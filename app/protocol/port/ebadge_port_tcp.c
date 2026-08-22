/**
 * @file    ebadge_port_tcp.c
 * @brief   Data-plane endpoint -- inbound is slots over SPI, outbound is an AT
 *          control command.  There is no socket on this chip.
 *
 * The protocol stack is written against a TCP listener (see ebadge_port_tcp.h),
 * but the phone's connection terminates on the 8711:
 *
 *     phone --Wi-Fi/TCP:9000--> 8711FA --SPI EBFS slots--> 8773G (us)
 *
 * So the two directions are asymmetric, and this file is thin in both:
 *
 *  - INBOUND never comes through here.  The 8711 forwards the file as EBFS slots
 *    on the SPI link, and wifi_xfer/ebfs_ingress.c calls the session entry point
 *    directly.  listen() therefore only records the port for logging; the
 *    on_data callback it is handed can never fire.  It is kept rather than
 *    deleted because it is what the sessions use to detect a failed bring-up,
 *    and because a firmware with a real socket would fill it in without touching
 *    the sessions.
 *
 *  - OUTBOUND is two AT commands, not a byte stream.  Protocol v2.2 sec.6.3
 *    moved the decision to this chip: after the last EBFS slot the 8711 holds
 *    the connection open and waits for AT+XFERACK (build the EBXR, close) or
 *    AT+XFERSTOP (close now, no EBXR).  It waits 120 s and then closes with no
 *    EBXR at all, which is the outcome to avoid -- the phone cannot tell a
 *    verify failure from a crash.
 *
 * ---------------------------------------------------------------------------
 * WHY THE ACK RETRIES AND ALMOST NOTHING ELSE HERE DOES
 * ---------------------------------------------------------------------------
 * The AT layer is single flight, so a submit can be refused with -EBUSY by
 * whatever else is on the wire -- in practice port_softap's poll.  For a query
 * that is harmless: it runs again next tick.  For the ack it is not, because
 * nothing else will ever send it and the 120 s window is the only thing between
 * a dropped ack and a phone left guessing.
 *
 * So -EBUSY here is retried from a delayed work item, a few times, spaced wider
 * than a single AT transaction takes (2..4 s on an idle link).  The retry does
 * NOT extend to a command the 8711 refused: an "[AT]:ERROR" means there is no
 * active file connection, i.e. the connection we wanted to answer is already
 * gone, and asking again cannot bring it back.
 */
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "ebadge_port_tcp.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include <zephyr/kernel.h>
#include "wifi_8711_at_xfer.h"
#endif

static ebadge_tcp_listen_t s_listener;
static bool                s_armed;

int ebadge_port_tcp_listen(const ebadge_tcp_listen_t *cfg)
{
    if (cfg == NULL)
    {
        return -1;
    }
    s_listener = *cfg;
    s_armed    = true;
    /* Nothing to open: the 8711 is already listening on this port -- it is the
     * port it reported to us, not one we chose.  Inbound bytes arrive via the
     * EBFS ingress, so s_listener.on_data stays unused by design. */
    EBADGE_LOG1("port_tcp: armed for port=%d (8711 owns the socket)",
                (int)cfg->port);
    return 0;
}

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  Pending control command, with a bounded retry for -EBUSY
 *----------------------------------------------------------------------------*/

/** Spaced wider than one AT transaction (2..4 s idle) so a retry does not just
 *  collide with the command that caused the -EBUSY. */
#define TCP_CTRL_RETRY_MS     500
#define TCP_CTRL_RETRY_MAX    8

typedef enum
{
    CTRL_NONE = 0,
    CTRL_ACK,
    CTRL_STOP,
} ctrl_kind_t;

static struct
{
    ctrl_kind_t kind;
    uint8_t     status;
    uint8_t     reason;
    uint8_t     tries;
} s_ctrl;

static void ctrl_retry_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_ctrl_work, ctrl_retry_work);

/** Fires on the transport thread once the 8711 has answered. */
static void ctrl_done(bool ok, void *user)
{
    (void)user;
    if (ok)
    {
        s_ctrl.kind = CTRL_NONE;
        return;
    }
    /* Refused or timed out.  Not retried -- see the header note.  The transfer
     * itself is not failed over this: the file is already stored and the App's
     * authoritative answer is the BLE 0x15/0x16 notify. */
    EBADGE_WARN("port_tcp: 8711 did not accept the transfer result");
    s_ctrl.kind = CTRL_NONE;
}

/** Stage whatever is pending.  Returns the submit rc. */
static int ctrl_submit(void)
{
    if (s_ctrl.kind == CTRL_ACK)
    {
        return wifi_8711_at_xfer_ack(s_ctrl.status, s_ctrl.reason,
                                     ctrl_done, NULL);
    }
    if (s_ctrl.kind == CTRL_STOP)
    {
        return wifi_8711_at_xfer_stop(ctrl_done, NULL);
    }
    return 0;
}

static void ctrl_retry_work(struct k_work *work)
{
    (void)work;

    if (s_ctrl.kind == CTRL_NONE)
    {
        return;
    }

    int rc = ctrl_submit();
    if (rc == 0)
    {
        return;
    }
    if (rc == -EBUSY && ++s_ctrl.tries < TCP_CTRL_RETRY_MAX)
    {
        (void)k_work_schedule(&s_ctrl_work, K_MSEC(TCP_CTRL_RETRY_MS));
        return;
    }
    EBADGE_WARN2("port_tcp: transfer result abandoned rc=%d after %u tries",
                 rc, (unsigned)s_ctrl.tries);
    s_ctrl.kind = CTRL_NONE;
}

/** Common path for both control commands: try now, retry on -EBUSY. */
static int ctrl_start(ctrl_kind_t kind, uint8_t status, uint8_t reason)
{
    /* A new command supersedes a pending one.  This happens when a session
     * aborts between staging an ack and it going out; the newer intent is the
     * correct one, and two of these must never be in flight together. */
    (void)k_work_cancel_delayable(&s_ctrl_work);

    s_ctrl.kind   = kind;
    s_ctrl.status = status;
    s_ctrl.reason = reason;
    s_ctrl.tries  = 0U;

    int rc = ctrl_submit();
    if (rc == 0)
    {
        return 0;
    }
    if (rc == -EBUSY)
    {
        EBADGE_LOG("port_tcp: AT layer busy, retrying transfer result");
        (void)k_work_schedule(&s_ctrl_work, K_MSEC(TCP_CTRL_RETRY_MS));
        return 0;
    }
    s_ctrl.kind = CTRL_NONE;
    return rc;
}

#endif /* CONFIG_WIFI_8711 */

int ebadge_port_tcp_ack(bool ok, uint8_t reason)
{
#if defined(CONFIG_WIFI_8711)
    /* Spec sec.10.3 requires reason==0 on success, and the 8711 copies both
     * bytes into the EBXR verbatim, so normalise here rather than let a caller's
     * stale reason travel alongside a success. */
    uint8_t status = ok ? WIFI_8711_XFER_STATUS_OK : WIFI_8711_XFER_STATUS_FAILED;
    uint8_t rsn    = ok ? 0U : reason;

    EBADGE_LOG2("port_tcp: -> XFERACK status=%u reason=%u",
                (unsigned)status, (unsigned)rsn);

    int rc = ctrl_start(CTRL_ACK, status, rsn);
    if (rc != 0)
    {
        /* -ENODEV on a board with no 8711.  Deliberately not fatal to the
         * transfer: the file is stored and BLE carries the real answer. */
        EBADGE_WARN1("port_tcp: XFERACK could not be staged rc=%d", rc);
    }
    return 0;
#else
    EBADGE_LOG2("port_tcp: no 8711 link, result (ok=%d reason=%u) dropped",
                (int)ok, (unsigned)reason);
    return 0;
#endif
}

int ebadge_port_tcp_abort(void)
{
#if defined(CONFIG_WIFI_8711)
    if (!s_armed)
    {
        /* No session of ours is up, so there is nothing to cut.  Sending
         * XFERSTOP anyway could kill a connection belonging to whatever started
         * after us, and the 8711 would answer ERROR in the common case. */
        return 0;
    }
    EBADGE_LOG("port_tcp: -> XFERSTOP (cut the stream, no EBXR)");
    (void)ctrl_start(CTRL_STOP, 0U, 0U);
    return 0;
#else
    return 0;
#endif
}

int ebadge_port_tcp_close(void)
{
    if (s_armed)
    {
        /* Nothing to tear down on this side -- the 8711 closes the connection
         * itself once it has answered the ack, or when the phone does.  Clearing
         * the flag keeps a late callback from being attributed to a session that
         * has ended. */
        EBADGE_LOG("port_tcp: disarmed");
    }
    s_armed = false;
    return 0;
}
