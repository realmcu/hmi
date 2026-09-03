/**
 * @file    ota_session.c
 * @brief   State machine for one 8711 firmware update.  See ota_session.h for the
 *          wire messages and for what "no integrity check" does and does not cost.
 *
 * Deadlines (mirroring xfer_session's, and for the same reasons):
 *
 *   WAIT_STA      60s   phone must open the TCP connection on our SoftAP
 *   RECV         120s   idle-in-transfer watchdog (no data seen)
 *   OVERALL      300s   OFFER to last received byte -- see OS_OVERALL_MS
 *   PUSH STALL   150s   backstop for a lost completion, NOT a transfer budget
 *
 * Driven by:
 *   - the 0xE0 handler (ota_session_offer)
 *   - port_softap's joined callback (already on l2_task; see the note at it)
 *   - port_tcp's on_data / on_close
 *   - port_ota's completion, which port_ota has already marshalled onto l2_task
 *   - the ebadge_task tick (~100 ms), for the four deadlines above
 *
 * ---------------------------------------------------------------------------
 * THE TWO PHASES DO NOT OVERLAP, AND THAT IS STRUCTURAL
 * ---------------------------------------------------------------------------
 * Receiving needs the antenna pointed at Wi-Fi and the AT link free for socket
 * traffic.  Pushing needs the AT link for one long transparent-mode command and
 * does not need the antenna at all -- the SPI link to the 8711 is a set of traces,
 * not RF.  So the sequence is: receive the whole image, tear the Wi-Fi data plane
 * down, give the antenna back to BLE, then push.
 *
 * The pleasant consequence is that BLE is on air for the entire push, which is the
 * slow part, so the App keeps its control link and gets the final verdict.  See
 * ota_cache.h for why a pass-through that avoided the buffering could not work.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>              /* snprintf, for the 0x16 detail string */
#include <stdbool.h>
#include <errno.h>

#include "ota_session.h"
#include "ota_cache.h"

#include "../wifi_xfer/xfer_session.h"
#include "../wifi_xfer/stream_session.h"
#include "../wifi_xfer/xfer_notify.h"

#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#include "../port/ebadge_port_antenna.h"
#include "../port/ebadge_port_softap.h"
#include "../port/ebadge_port_tcp.h"
#include "../port/ebadge_port_ota.h"

/*----------------------------------------------------------------------------*
 *  Config
 *----------------------------------------------------------------------------*/
#define OS_WAIT_STA_MS        60000u
#define OS_RECV_IDLE_MS      120000u

/* Whole-session cap, measured from the offer and STOPPED when the push begins.
 *
 * Larger than xfer_session's 180 s because the payload is larger: an OTA image is
 * a megabyte or more where a wallpaper is a couple of hundred kilobytes, and the
 * link is the same one.  Under-budgeting this would abandon transfers that were
 * progressing perfectly well.
 *
 * It does not cover the push, which is watchdogged separately below -- a cap that
 * spanned both would have to be the sum of two very different worst cases and
 * would then be too loose to catch either. */
#define OS_OVERALL_MS        300000u

/* How long the push may make no visible progress before we give up on it.
 *
 * This is NOT how long a push is allowed to take.  at_client already bounds every
 * step of the transfer itself -- the prompt, each 16 KB chunk, and the final OK --
 * and reports a failure through our completion callback when one of those expires.
 * So the only thing left for this to catch is a completion that never arrives at
 * all, which is a bug rather than a timeout.
 *
 * The value has to clear the longest legitimate silence, and that is the final
 * wait: the 8711 answers AT+OTAUSER only after writing the whole image to its own
 * flash, sending nothing meanwhile, so the sent counter sits still for as long as
 * that takes (AT_CLIENT_STREAM_TERM_MS allows a minute).  150 s leaves margin
 * without making a genuine wedge cost the user minutes. */
#define OS_PUSH_STALL_MS     150000u

/* Progress throttle, same rule as the file path (spec §4.10). */
#define OS_PROGRESS_MS           200
#define OS_PROGRESS_STEP_PCT       5

/*----------------------------------------------------------------------------*
 *  Session state  (l2_task-owned; no locking)
 *----------------------------------------------------------------------------*/
typedef struct
{
    ota_session_state_t state;

    uint32_t size;                /* declared by the offer                    */
    uint16_t tcp_port;            /* as reported by the AP, not chosen        */
    uint32_t bytes_recv;

    /* deadlines, in ebadge_task_now_ms units */
    uint32_t deadline_stage;
    uint32_t deadline_overall;
    uint32_t last_data_ms;
    uint32_t last_progress_ms;
    uint8_t  last_progress_pct;

    /* Push watchdog: the last counter value we saw and when we saw it move. */
    uint32_t push_sent;
    uint32_t push_moved_ms;
} ota_ctx_t;

static ota_ctx_t s_o;

/* Defined next to the push it starts, but called from the receive path above it. */
static void start_push(void);

/*----------------------------------------------------------------------------*
 *  Transitions
 *----------------------------------------------------------------------------*/
static void reset_ctx(void)
{
    memset(&s_o, 0, sizeof(s_o));
    s_o.state = OTA_SESSION_IDLE;
}

/**
 * Drop the Wi-Fi data plane and hand the antenna back.
 *
 * Called on every exit from an accepted session, including the successful one --
 * which is the difference from xfer_session, where the success path holds the
 * connection open to ack on it.  A raw image has no data-plane verdict to send,
 * so there is nothing to keep the socket for.
 *
 * The cache is NOT reset here.  On the success path the push is about to borrow it
 * (see ota_cache_data()), and on the failure paths the caller resets it -- one
 * place decides, rather than two that have to agree.
 */
static void tear_down_data_plane(void)
{
    (void)ebadge_port_tcp_close();
    if (ebadge_port_softap_running())
    {
        (void)ebadge_port_softap_stop();
    }
    ebadge_port_antenna_release_wifi("ota teardown");
}

/**
 * Fail an ACCEPTED session: 0x16 with a §2.6 reason, then back to idle.
 *
 * Only for after 0x13 has gone out.  Before that the offer is answered with 0x04
 * RESULT instead -- see the wire-message table in the header, and refuse() below.
 *
 * @p detail must have static lifetime; every caller passes a literal.
 */
static void fail_and_reset(uint8_t reason, const char *detail)
{
    (void)ebadge_port_tcp_abort();
    tear_down_data_plane();
    /* The half-received image must not survive: the next offer starts appending at
     * the cursor, so leaving it would build one image out of two. */
    ota_cache_reset();

    eb_emit_fail(reason, detail);
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Report first, then schedule the radio down -- same ordering rule as the file
     * path, for the same reason (see emit_held_verdict() in xfer_session.c): the
     * 0x16 above has only just been queued, and an App that retries on it re-claims
     * the AP inside the settling window and pays no bring-up. */
    ebadge_port_softap_shutdown_when_idle();
#endif
    reset_ctx();
}

/** Refuse an offer before anything was promised: one 0x04 RESULT, nothing else. */
static void refuse(uint8_t result_code, const char *why)
{
    EBADGE_WARN1("ota: offer refused (%s)", why);
    (void)ebadge_l2_result_send(EB_CMD_OTA_OFFER, result_code);
}

/*----------------------------------------------------------------------------*
 *  Data-plane callbacks
 *----------------------------------------------------------------------------*/
static void on_softap_joined_from_driver(void)
{
    /* Already on l2_task -- port_tcp marshals the at_socket connect event before
     * port_softap ever sees it, so a post_call here would only add a hop.  Same as
     * the file path; the note there explains why "a station joined" now means "the
     * phone opened its TCP connection". */
    ota_session_on_sta_joined();
}

/* Unpacked by the trampoline below.  Two mallocs per delivery, like the file
 * path: the chunk pointer and its length cannot both fit in one void*. */
struct ota_chunk { uint8_t *p; uint16_t n; };

static void ota_data_trampoline(void *arg)
{
    struct ota_chunk *c = arg;
    if (c == NULL) { return; }

    if (s_o.state == OTA_SESSION_RECV)
    {
        uint32_t now = ebadge_task_now_ms();
        s_o.last_data_ms = now;

        /* Straight into the cache.  No ingress module in between, and nothing to
         * strip: the image is raw, so there is no framing to parse and no frame
         * boundary to find.  That is the whole reason this path is three lines
         * where the file path needs ebfs_ingress and the preview needs jpgs. */
        int rc = ota_cache_append(c->p, c->n);
        if (rc < 0)
        {
            /* -ENOSPC here means the sender has gone past the size it declared;
             * the offer already refused anything the cache could not hold. */
            EBADGE_ERR2("ota: cache append failed rc=%d len=%d", rc, (int)c->n);
            fail_and_reset(rc == -ENOSPC ? EB_XFER_ERR_TOO_LARGE
                           : EB_XFER_ERR_IO_TIMEOUT,
                           "cache append");
            goto out;
        }
        s_o.bytes_recv += c->n;

        /* §4.10 throttle: a 5% boundary or 200 ms, whichever comes first. */
        uint8_t pct = (s_o.size != 0U)
                      ? (uint8_t)((uint64_t)s_o.bytes_recv * 100u / s_o.size)
                      : 0u;
        bool step_hit = pct >= (uint8_t)(s_o.last_progress_pct +
                                         OS_PROGRESS_STEP_PCT);
        bool time_hit = (int32_t)(now - (s_o.last_progress_ms +
                                         OS_PROGRESS_MS)) >= 0;
        if (step_hit || time_hit)
        {
            eb_emit_progress(s_o.bytes_recv, s_o.size);
            s_o.last_progress_pct = pct;
            s_o.last_progress_ms  = now;
        }

        if (s_o.bytes_recv >= s_o.size)
        {
            start_push();
        }
    }

out:
    free(c->p);
    free(c);
}

static void on_tcp_data_from_driver(const uint8_t *data, uint16_t len)
{
    /* CONTEXT: the SPI transport thread, and `data` points into the slot buffer
     * that is re-armed for DMA the moment this returns.  So a heap copy plus a hop
     * onto l2_task, both mandatory -- session state has no locks and l2_task
     * (priority 3) preempts the transport thread (5). */
    if (len == 0U) { return; }

    uint8_t *copy = (uint8_t *)malloc(len);
    if (copy == NULL) { return; }
    memcpy(copy, data, len);

    struct ota_chunk *c = (struct ota_chunk *)malloc(sizeof(*c));
    if (c == NULL) { free(copy); return; }
    c->p = copy;
    c->n = len;

    if (ebadge_task_post_call(ota_data_trampoline, c) != 0)
    {
        free(copy);
        free(c);
    }
}

static void on_tcp_close_from_driver(ebadge_tcp_close_reason_t r)
{
    (void)ebadge_task_post_call((ebadge_post_fn_t)ota_session_on_tcp_close,
                                (void *)(uintptr_t)r);
}

/*----------------------------------------------------------------------------*
 *  Push completion  (port_ota has already marshalled this onto l2_task)
 *----------------------------------------------------------------------------*/

/**
 * Map the module's error number onto a §2.6 transfer reason.
 *
 * The mapping is lossy and there is no honest way around that: §2.6 was written
 * for file transfer and has no OTA-specific codes at all.  So the reason byte is
 * chosen for what the App should DO about it, and the exact module error travels
 * in the 0x16 detail string, which is where a reader should look.
 */
static uint8_t reason_for_module_error(int error_no)
{
    /* Negative is not one of the module's numbers: at_client hands its own errno
     * up when the command never got far enough to have a reply, so the module may
     * not have seen the image at all.  VERIFY would be a lie in the App's face --
     * it says "your file is bad, do not resend it" -- when the truthful answer is
     * "the link under us failed, try again".  This is where a dropped SPI chunk
     * lands, and it is the one failure the bench has actually produced. */
    if (error_no < 0)
    {
        return EB_XFER_ERR_IO_TIMEOUT;
    }

    switch (error_no)
    {
    case 4:   return EB_XFER_ERR_BUSY;          /* module already updating     */
    case 2:   return EB_XFER_ERR_STORAGE_FULL;  /* module could not allocate   */
    case 0:   return EB_XFER_ERR_IO_TIMEOUT;    /* AT-level failure, no code   */
    default:  return EB_XFER_ERR_VERIFY;        /* 1/3/6: it refused the image */
    }
}

/* Signature matches ebadge_ota_done_cb_t.  port_ota promises l2_task context, so
 * this is registered directly rather than behind another post_call hop. */
static void push_done(bool ok, int error_no)
{
    if (s_o.state != OTA_SESSION_PUSHING)
    {
        /* Late completion for a session that has already gone -- the stall
         * watchdog fired, or a GAP disconnect reset us while the push ran on.  The
         * image still reached the module either way, which is why abort() lets a
         * push finish; there is simply nobody left to tell. */
        EBADGE_WARN2("ota: push completed after the session ended (ok=%d err=%d)",
                     (int)ok, error_no);
        ota_cache_reset();
        return;
    }

    /* The push is over, so the borrowed image is free to go. */
    ota_cache_reset();

    if (!ok)
    {
        static char detail[EB_MAX_MSG_STR + 1];
        (void)snprintf(detail, sizeof(detail), "8711 ota err %d", error_no);
        /* Two different failures, and conflating them cost a bench session: a
         * positive number is the module's own refusal, a negative one is our SPI
         * link giving up before the module ever ruled on the image. */
        if (error_no < 0)
        {
            EBADGE_ERR1("ota: push failed on the SPI link (%d) -- the module "
                        "never ruled on the image", error_no);
        }
        else
        {
            EBADGE_ERR1("ota: module refused the image (err=%d)", error_no);
        }
        /* Not fail_and_reset(): the Wi-Fi data plane went down before the push
         * started, so there is nothing to tear down and no socket to abort. */
        eb_emit_fail(reason_for_module_error(error_no), detail);
        reset_ctx();
        return;
    }

    /* The module has the image in its staging bank; only a restart makes it the
     * running firmware.  Fire and forget -- success is the module vanishing, so
     * there is no answer to wait for, and everything it was holding (SoftAP, TCP
     * server) has already been torn down above. */
    int rc = ebadge_port_ota_restart();
    if (rc != 0)
    {
        /* The image is good and staged, but it is not running yet.  Reported as a
         * failure because that is the truthful answer to "did the update happen?",
         * and the recovery -- power-cycle, or retry -- is the user's. */
        EBADGE_ERR1("ota: image accepted but restart refused rc=%d", rc);
        eb_emit_fail(EB_XFER_ERR_IO_TIMEOUT, "restart refused");
        reset_ctx();
        return;
    }

    EBADGE_LOG1("ota: module took %u bytes and is restarting -> SUCCEED",
                (unsigned)s_o.size);
    (void)ebadge_l2_result_send(EB_CMD_OTA_OFFER, EB_RESULT_SUCCEED);
    reset_ctx();
}

/*----------------------------------------------------------------------------*
 *  RECV -> PUSHING
 *----------------------------------------------------------------------------*/
/** The last image byte has arrived.  Shut Wi-Fi down, then start the SPI push. */
static void start_push(void)
{
    if (s_o.state != OTA_SESSION_RECV) { return; }

    /* An overrun is worth naming even though nothing here verifies the image: it
     * means the sender and the offer disagree about the size, and the size is the
     * one number AT+OTAUSER cannot get wrong -- it is declared in the command line
     * before the first byte goes out. */
    if (ota_cache_len() != s_o.size)
    {
        EBADGE_ERR2("ota: size mismatch got=%u offered=%u",
                    (unsigned)ota_cache_len(), (unsigned)s_o.size);
        fail_and_reset(EB_XFER_ERR_VERIFY, "size mismatch");
        return;
    }

    /* Final progress notify, so the App's bar reaches 100% before the silence that
     * the header warns about.  Unconditional: the throttle may well have skipped
     * the last chunk, and this is the one sample the App is sure to want. */
    eb_emit_progress(s_o.bytes_recv, s_o.size);

    /* Wi-Fi down BEFORE the push, and this ordering is the design rather than
     * tidiness: the push occupies the AT link with one command that does not
     * return for tens of seconds, and socket traffic on the same link would be
     * queued behind it.  Tearing down first also gives the antenna back, so BLE is
     * on air for the whole push and can carry the verdict. */
    tear_down_data_plane();

    EBADGE_LOG1("ota: %u bytes received -> pushing to the module over SPI AT",
                (unsigned)s_o.size);

    s_o.state         = OTA_SESSION_PUSHING;
    s_o.push_sent     = 0U;
    s_o.push_moved_ms = ebadge_task_now_ms();

    int rc = ebadge_port_ota_push(ota_cache_data(), ota_cache_len(), push_done);
    if (rc != 0)
    {
        EBADGE_ERR1("ota: push refused rc=%d", rc);
        ota_cache_reset();
        eb_emit_fail(rc == -ENOTSUP ? EB_XFER_ERR_FMT_UNSUPPORTED
                     : EB_XFER_ERR_IO_TIMEOUT,
                     "push refused");
        reset_ctx();
    }
}

/*----------------------------------------------------------------------------*
 *  Tick -- runs on l2_task every EBADGE_TICK_MS
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
    if (s_o.state == OTA_SESSION_IDLE)
    {
        return;
    }

    if (s_o.state == OTA_SESSION_PUSHING)
    {
        /* The push has its own clock and the overall cap deliberately does not
         * apply to it -- see OS_PUSH_STALL_MS for why this is a stall detector and
         * not a budget. */
        uint32_t sent = ebadge_port_ota_sent();
        if (sent != s_o.push_sent)
        {
            s_o.push_sent     = sent;
            s_o.push_moved_ms = now_ms;
        }
        else if ((int32_t)(now_ms - (s_o.push_moved_ms + OS_PUSH_STALL_MS)) >= 0)
        {
            EBADGE_ERR1("ota: push made no progress for %u s -- giving up on the "
                        "completion", (unsigned)(OS_PUSH_STALL_MS / 1000u));
            /* Deliberately NOT resetting the cache: port_ota may still be holding
             * the image, and pulling it out from under a live transfer would put
             * whatever lands there next onto the SPI link.  The late completion
             * releases it -- see push_done(). */
            eb_emit_fail(EB_XFER_ERR_IO_TIMEOUT, "push stalled");
            reset_ctx();
        }
        return;
    }

    if ((int32_t)(now_ms - s_o.deadline_overall) >= 0)
    {
        EBADGE_WARN("ota: overall deadline hit");
        fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "overall timeout");
        return;
    }

    switch (s_o.state)
    {
    case OTA_SESSION_WAIT_STA:
        if ((int32_t)(now_ms - s_o.deadline_stage) >= 0)
        {
            EBADGE_WARN("ota: STA did not join in 60s");
            fail_and_reset(EB_XFER_ERR_STA_TIMEOUT, "sta join timeout");
        }
        break;

    case OTA_SESSION_RECV:
        if ((int32_t)(now_ms - (s_o.last_data_ms + OS_RECV_IDLE_MS)) >= 0)
        {
            EBADGE_WARN("ota: RECV idle, bail");
            fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "recv idle");
        }
        break;

    case OTA_SESSION_PUSHING:
    case OTA_SESSION_IDLE:
    default:
        break;
    }
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void ota_session_init(void)
{
    reset_ctx();
    ebadge_task_set_tick(on_tick);
}

ota_session_state_t ota_session_state(void) { return s_o.state; }
uint32_t ota_session_bytes_received(void)   { return s_o.bytes_recv; }
uint32_t ota_session_total_size(void)       { return s_o.size; }

void ota_session_offer(uint32_t size)
{
    /* --- refusals, all answered with one 0x04 RESULT and nothing else ------- */
    if (s_o.state != OTA_SESSION_IDLE)
    {
        refuse(EB_RESULT_BUSY, "an update is already running");
        return;
    }
    /* A push from a PREVIOUS session can outlive that session: the stall watchdog
     * and the GAP-disconnect path both reset the context while port_ota keeps the
     * transfer running, precisely so a half-written image is never left in the
     * module.  Accepting a new offer then would start overwriting the cache the
     * live push is reading from, so this check is not redundant with the state
     * test above -- it is the one that makes those two designs safe. */
    if (ebadge_port_ota_busy())
    {
        refuse(EB_RESULT_BUSY, "the previous push has not finished");
        return;
    }
    /* One radio, one SoftAP, one TCP port -- the same mutual exclusion the other
     * two sessions apply to each other (spec §6.7). */
    if (xfer_session_state() != XFER_SESSION_IDLE)
    {
        refuse(EB_RESULT_BUSY, "a file transfer is running");
        return;
    }
    if (stream_session_state() != STREAM_SESSION_IDLE)
    {
        refuse(EB_RESULT_BUSY, "a preview stream is running");
        return;
    }
    if (size == 0U)
    {
        refuse(EB_RESULT_FAILED, "size=0");
        return;
    }
    if (size > ota_cache_capacity())
    {
        /* Stated up front rather than discovered part-way: accepting would bring
         * the AP up, run until ota_cache_append() refused a chunk, and then report
         * a limit we knew about before the transfer started. */
        EBADGE_WARN2("ota: size=%u > cache %u",
                     (unsigned)size, (unsigned)ota_cache_capacity());
        refuse(EB_RESULT_FAILED, "image larger than the staging buffer");
        return;
    }

    /* --- accepted ---------------------------------------------------------- */
    reset_ctx();
    s_o.size = size;
    ota_cache_reset();

    EBADGE_LOG1("ota: offer accepted, image=%u bytes", (unsigned)size);

    ebadge_softap_info_t info;
    uint16_t             tcp_port = 0;

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Ask for the radio before asking about it.  Cannot make the AP ready for THIS
     * offer -- a bring-up is seconds to tens of seconds -- but it cancels a pending
     * idle-down and makes the AP_START refusal below worth retrying.  Placed after
     * every refusal path, so an offer we reject never raises a radio that nothing
     * would then schedule down. */
    ebadge_port_softap_arm_on_l2("ota offer accepted");
#endif

    int rc = ebadge_port_softap_start(&info, &tcp_port,
                                      on_softap_joined_from_driver);
    if (rc != 0)
    {
        /* Nothing has been promised yet, so this is still a refusal rather than a
         * 0x16: NOT_READY says "ask again", which is exactly the recovery -- the AP
         * is down, starting, or its state feed has stopped, and all three clear on
         * their own. */
        EBADGE_ERR1("ota: softap_start FAIL (%d)", rc);
        refuse(EB_RESULT_NOT_READY, "softap not up");
        reset_ctx();
        return;
    }
    s_o.tcp_port = tcp_port;

    /* The 8711 runs the TCP server itself, so there is no socket to open here --
     * port_tcp only arms the sink.  Which session parses the bytes is decided by
     * who armed it, NOT by the port number: all three share port 9000 and rely on
     * being mutually exclusive for that to be unambiguous. */
    ebadge_tcp_listen_t lc =
    {
        .port     = tcp_port,
        .on_data  = on_tcp_data_from_driver,
        .on_close = on_tcp_close_from_driver,
    };
    if (ebadge_port_tcp_listen(&lc) != 0)
    {
        EBADGE_ERR("ota: tcp_listen FAIL");
        (void)ebadge_port_softap_stop();
        refuse(EB_RESULT_NOT_READY, "tcp listener not armed");
        reset_ctx();
        return;
    }

    /* THE CREDENTIALS ARE THE ACCEPTANCE (see the header's wire-message table).
     * Nothing else is emitted on this path -- an App that receives 0x13 for a 0xE0
     * it sent has been told yes. */
    EBADGE_LOG("ota: emitting AP_INFO (firmware update -> the shared port)");
    eb_emit_ap_info(&info, s_o.tcp_port);

    /* Antenna to Wi-Fi only now.  Moving it earlier would send the 0x13 with the
     * radio already pointed away from BLE, i.e. ask the phone to join a hotspot it
     * was never told about.  Released by tear_down_data_plane(), on every exit. */
    ebadge_port_antenna_claim_wifi("ota data plane");

    uint32_t now = ebadge_task_now_ms();
    s_o.state            = OTA_SESSION_WAIT_STA;
    s_o.deadline_stage   = now + OS_WAIT_STA_MS;
    s_o.deadline_overall = now + OS_OVERALL_MS;
    s_o.last_progress_ms = now;
    EBADGE_LOG("ota: WAIT_STA (60s)");
}

void ota_session_on_sta_joined(void)
{
    if (s_o.state == OTA_SESSION_RECV)
    {
        /* Data beat the notification and promoted us already -- redundant, not
         * wrong, so it must not warn. */
        return;
    }
    if (s_o.state != OTA_SESSION_WAIT_STA)
    {
        EBADGE_WARN1("ota: sta_joined in wrong state=%d", (int)s_o.state);
        return;
    }

    ota_cache_reset();
    s_o.bytes_recv   = 0U;
    s_o.state        = OTA_SESSION_RECV;
    s_o.last_data_ms = ebadge_task_now_ms();
    EBADGE_LOG("ota: RECV started (buffering the image to PSRAM)");
}

void ota_session_on_tcp_close(int reason)
{
    if (s_o.state == OTA_SESSION_IDLE || s_o.state == OTA_SESSION_PUSHING)
    {
        /* Expected: we close the connection ourselves on the way into the push. */
        return;
    }
    EBADGE_WARN1("ota: tcp closed mid-session reason=%d", reason);
    fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "tcp closed");
}

void ota_session_abort(void)
{
    if (s_o.state == OTA_SESSION_IDLE)
    {
        return;
    }

    if (s_o.state == OTA_SESSION_PUSHING)
    {
        /* Let it finish.  The image is already off the phone and out of the Wi-Fi
         * path, the AT link does not need BLE, and stopping now would leave the
         * module holding a partial image -- strictly worse than completing.  What
         * we do give up is the reporting: reset_ctx() makes the eventual completion
         * take the "session has already gone" branch in push_done(), and the
         * restart it would have
         * triggered is dropped with it.
         *
         * The cache is left alone for the same reason as in the stall path: the
         * push is still reading it.  The completion resets it, and until then
         * ebadge_port_ota_busy() refuses any new offer. */
        EBADGE_WARN("ota: abort during push -- letting the module finish, "
                    "no verdict will be reported");
        reset_ctx();
        return;
    }

    EBADGE_LOG("ota: abort");
    (void)ebadge_port_tcp_abort();
    tear_down_data_plane();
    ota_cache_reset();
    /* No notify: this comes from the GAP disconnect hook, so the BLE link is
     * probably already gone.  A later offer starts fresh. */
    reset_ctx();
}
