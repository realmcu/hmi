/**
 * @file    xfer_session.c
 * @brief   State machine for one in-flight WiFi file transfer.
 *
 * Timing / timeouts (see PROT-001 §5.5):
 *
 *   WAIT_CONFIRM   30s   transient in V1.3 -- §4.7 auto-accepts, so this
 *                        deadline only guards a reinstated user prompt
 *   WAIT_STA       60s   phone STA must associate with our SoftAP
 *   RECV          120s   idle-in-transfer watchdog (no data seen)
 *   OVERALL       180s   whole thing from OFFER to DONE
 *
 * The state machine is driven by:
 *   - BLE handlers (offer, which now auto-resolves user_decision)
 *   - port_softap callback (on_sta_joined)
 *   - ebfs_ingress (on_payload, check_identity) -- the live data path
 *   - port_tcp callback (on_data, on_close) -- only with a real socket
 *   - ebadge_task tick (~100ms) -- for the four timeouts above
 *
 * All callbacks from other threads MUST be marshalled via
 * ebadge_task_post_call() before invoking anything here.
 *
 * Progress throttling (PROT-001 §4.10):  emit 0x14 PROGRESS at least every
 * 200ms OR at each new 5% boundary, whichever comes first.
 *
 * ---------------------------------------------------------------------------
 * THREE WAYS A SESSION ENDS, AND WHY THEY ARE NOT ONE FUNCTION
 * ---------------------------------------------------------------------------
 * Since SPI protocol v2.2 the data plane distinguishes "here is the result" from
 * "stop sending", so this file does too:
 *
 *   success        ack(ok) then close.  Only after CRC and flash commit both
 *                  pass -- that is what makes the result true.
 *   fail_and_reset ack(fail, reason) then close.  For a verdict reached on a
 *                  complete transfer: bad CRC, storage refused the commit,
 *                  a deadline expired.
 *   abort_and_reset cut the stream, no verdict.  For when receiving the rest is
 *                  pointless: the two planes disagree about which file this is,
 *                  or the session is being torn down under us.
 *
 * The middle one is not a special case of the last: a phone that gets a coded
 * failure can tell the user why, whereas one whose connection is simply cut
 * cannot -- so the verdict is worth sending whenever there is one to send.
 */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "xfer_session.h"
#include "stream_session.h"
#include "xfer_notify.h"
#include "ebxf_frame.h"
#include "jpgs_ingress.h"
#include "ebfs_ingress.h"

#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#include "../port/ebadge_port_softap.h"
#include "../port/ebadge_port_tcp.h"
#include "../port/ebadge_port_storage.h"

/*----------------------------------------------------------------------------*
 *  Config
 *----------------------------------------------------------------------------*/
#define XS_WAIT_CONFIRM_MS   30000u
#define XS_WAIT_STA_MS       60000u
#define XS_RECV_IDLE_MS     120000u
#define XS_OVERALL_MS       180000u

/* How long the BLE verdict waits for the data-plane ack before going out anyway.
 *
 * The ack is retried for TCP_CTRL_RETRY_MAX * TCP_CTRL_RETRY_MS (~4 s) plus one
 * AT transaction, and an AT round trip has been seen at 11..13 s on hardware
 * when it queues behind a WLSTATE poll.  20 s covers that with margin and still
 * leaves the App an answer well inside its own patience.
 *
 * This is a backstop, not the normal path: port_tcp settles every ack exactly
 * once, so reaching this deadline means the completion was lost, not merely
 * slow.  Without it a lost completion would park the session in COMPLETING for
 * good -- the overall deadline below fires first in practice, but it would
 * report a timeout for a transfer that actually succeeded. */
#define XS_ACK_WAIT_MS       20000u

/* The TCP port is NOT ours to choose -- the 8711 runs the server and reports the
 * upload port in its WLSTATE reply (9000 in practice; 5004 is the separate
 * preview port).  EB_AP_DEFAULT_PORT is kept out of this file deliberately so
 * nobody reintroduces a hardcoded one. */
#define XS_PROGRESS_MS         200
#define XS_PROGRESS_STEP_PCT     5

/* Largest file the upload data plane can carry, in bytes.
 *
 * 2 MiB is the 8711's port-9000 single-file cap (SPI spec v2.2 §6), and the BLE
 * spec's §8.3 suggests the same figure pending a final value.
 *
 * This used to be 61440, which was a real limit copied from the wrong channel:
 * 60 KiB is the *frame* cap on the preview port 5004, where a whole JPEG crosses
 * as one frame.  A file on port 9000 is fragmented into EBFS slots by the 8711
 * and has no such ceiling, so the old value refused ordinary wallpapers with
 * TOO_LARGE.  The two caps must stay distinct -- see WIFI_8711_FILE_SIZE_MAX
 * and WIFI_8711_JPEG_FRAME_MAX.
 *
 * Deliberately a literal rather than the driver macro: that one lives behind
 * CONFIG_WIFI_8711 and this file is transport agnostic on purpose (it also
 * serves the 0x02 BLE path).  A BUILD_ASSERT would be better but would drag the
 * driver header in; the comment above is the contract. */
#define XS_MAX_FILE_SIZE     (2u * 1024u * 1024u)

/* Framing bytes reserved on flash in front of the file content.
 *
 * The EBXF path stores the whole TCP packet -- 40-byte header plus body -- so the
 * reservation must cover both.  The JPGS path stores no framing at all (the 8711
 * strips it), and this is still reserved there because wp_begin runs on the
 * WAIT_STA -> RECV edge, before any data has arrived to say which path this
 * transfer will take.  Over-reserving by 40 bytes is the cheap side of that
 * guess; under-reserving would fail the last write of a max-size file.
 *
 * What actually got stored is reported to wp_commit as the content offset, and
 * that comes from ebxf_seen rather than from here. */
#define XS_EBXF_STORED_LEN   EBXF_HDR_LEN

/*----------------------------------------------------------------------------*
 *  Session state  (l2_task-owned; no locking)
 *----------------------------------------------------------------------------*/
typedef struct
{
    xfer_session_state_t state;

    /* offer parameters ------------------------------------------------- */
    char     name[EBXF_NAME_LEN + 1];
    uint8_t  file_type;
    uint32_t size;
    uint32_t crc32_expected;
    uint16_t replace_id;

    /* data plane ------------------------------------------------------- */
    uint16_t tcp_port;            /* as reported by the AP, not chosen    */

    /* runtime ---------------------------------------------------------- */
    uint32_t bytes_recv;
    uint32_t crc32_running;
    bool     ebxf_seen;
    int      wp_handle;

    /* Verdict held while the data-plane ack goes out.
     *
     * Both planes must report, and the data plane goes first (see the ordering
     * note on ack_settled()).  The BLE notify is therefore built after the ack
     * settles, on a callback with no access to the frame that decided the
     * outcome -- so what it needs is parked here.
     *
     * done_pending distinguishes "waiting for an ack" from "COMPLETING for some
     * other reason", which is what lets the tick put a deadline on the wait
     * without arming one for every completion. */
    bool     done_pending;
    bool     done_ok;
    uint8_t  done_reason;         /* §2.6 code; meaningful when !done_ok   */
    uint16_t done_file_id;
    /* Static-lifetime string literal from the failing call site; only read
     * after the ack settles, which is why it cannot be a buffer we own. */
    const char *done_detail;
    uint32_t deadline_ack;        /* only while done_pending               */

    /* Partial EBXF header carried across deliveries.  The 40-byte header is
     * not guaranteed to arrive in one piece: it rides the same byte stream as
     * the body, and the EBFS transport below it chops that stream into 4032-byte
     * chunks with no regard for where the header ends, so a header split across
     * two deliveries is routine rather than anomalous.  Bytes accumulate here
     * until all EBXF_HDR_LEN are in hand. */
    uint8_t  ebxf_hdr[EBXF_HDR_LEN];
    uint8_t  ebxf_hdr_len;

    /* timers (deadlines in ebadge_task_now_ms units) ------------------ */
    uint32_t deadline_stage;      /* per-state deadline                  */
    uint32_t deadline_overall;    /* whole-session hard cap              */
    uint32_t last_data_ms;        /* rolls the RECV-idle deadline        */
    uint32_t last_progress_ms;
    uint8_t  last_progress_pct;
} xfer_ctx_t;

static xfer_ctx_t s_x;

/*----------------------------------------------------------------------------*
 *  Local helpers -- notify emission
 *
 *  Every builder below follows the TLV order given in the spec section named
 *  in its comment.  Type numbers are per-command (spec §2.4), so the macro
 *  prefix must match the command being built -- EB_TLV_DEC_* only inside
 *  0x11, EB_TLV_AP_* only inside 0x13, and so on.
 *----------------------------------------------------------------------------*/

/** 0x11 XFER_DECISION (spec §4.8).  @p reason is optional: pass 0 to omit. */
static void emit_decision(uint8_t decision, uint8_t reason)
{
    uint8_t  params[16];
    uint16_t off = 0;
    ebadge_tlv_put_u8(params, sizeof(params), &off,
                      EB_TLV_DEC_DECISION, decision);
    if (reason != 0)
    {
        ebadge_tlv_put_u8(params, sizeof(params), &off,
                          EB_TLV_DEC_REASON, reason);
    }
    (void)ebadge_l2_notify_send(EB_CMD_XFER_DECISION, params, off);
}

/** 0x13 AP_INFO and 0x16 XFER_FAIL are byte-identical for the preview stream,
 *  so they live in xfer_notify.c -- eb_emit_ap_info() / eb_emit_fail().  These
 *  thin wrappers keep the call sites below reading as before and pin the port,
 *  which the shared builder takes as a parameter.                          */
static void emit_ap_info(const ebadge_softap_info_t *info)
{
    eb_emit_ap_info(info, s_x.tcp_port);
}

static void emit_fail(uint8_t reason, const char *detail)
{
    eb_emit_fail(reason, detail);
}

/** 0x14 XFER_PROGRESS (spec §4.10) -- absolute byte counters, not a percent. */
static void emit_progress(uint32_t recv, uint32_t total)
{
    uint8_t  params[16];
    uint16_t off = 0;
    ebadge_tlv_put_u32(params, sizeof(params), &off, EB_TLV_PROG_RECV,  recv);
    ebadge_tlv_put_u32(params, sizeof(params), &off, EB_TLV_PROG_TOTAL, total);
    (void)ebadge_l2_notify_send(EB_CMD_XFER_PROGRESS, params, off);
}

/** 0x15 XFER_DONE (spec §4.11) -- all three TLVs required. */
static void emit_done(uint16_t file_id)
{
    uint8_t  params[64];
    uint16_t off = 0;
    ebadge_tlv_put_u16(params, sizeof(params), &off,
                       EB_TLV_DONE_FILE_ID, file_id);
    ebadge_tlv_put_u32(params, sizeof(params), &off,
                       EB_TLV_DONE_SIZE, s_x.size);
    ebadge_tlv_put(params, sizeof(params), &off,
                   EB_TLV_DONE_NAME,
                   (const uint8_t *)s_x.name,
                   (uint16_t)strlen(s_x.name));
    (void)ebadge_l2_notify_send(EB_CMD_XFER_DONE, params, off);
}

/*----------------------------------------------------------------------------*
 *  State transitions
 *----------------------------------------------------------------------------*/
static void reset_ctx(void)
{
    memset(&s_x, 0, sizeof(s_x));
    s_x.state     = XFER_SESSION_IDLE;
    s_x.wp_handle = 0;
}

static void tear_down_data_plane(void)
{
    if (ebadge_port_softap_running())
    {
        (void)ebadge_port_softap_stop();
    }
    (void)ebadge_port_tcp_close();
    /* Drop any half-reassembled frame or file too.  The 8711 may well be
     * mid-transfer when we give up, and leftover chunks measured against
     * something this session never started would make the *next* transfer's
     * first slot look corrupt. */
    jpgs_ingress_reset();
    ebfs_ingress_reset();
    if (s_x.wp_handle > 0)
    {
        (void)ebadge_port_storage_wp_abort(s_x.wp_handle);
        s_x.wp_handle = 0;
    }
}

/**
 * Emit the held BLE verdict and end the session.  Runs on l2_task.
 *
 * ---------------------------------------------------------------------------
 * WHY THE BLE VERDICT WAITS FOR THE DATA-PLANE ONE
 * ---------------------------------------------------------------------------
 * Both planes report the outcome, and BLE is the authoritative one (spec §5.3).
 * But BLE is also the FASTER one: a notify is a couple of milliseconds, whereas
 * the ack is an AT transaction that may be queued behind port_softap's poll and
 * retried for seconds.  Emitted in the obvious order, the phone would routinely
 * be told "transfer done" over BLE while the TCP upload it is still holding open
 * had heard nothing -- and an App that closes its socket on the BLE notify would
 * cut the connection out from under the ack, so the ack would then fail against
 * a connection that no longer exists.
 *
 * So the ack goes first and this runs when it settles.  "Settles", not
 * "succeeds": a refused or abandoned ack still gets here, because the BLE
 * verdict is the one the App is required to believe and withholding it over a
 * data-plane failure would lose the only answer that counts.
 */
static void emit_held_verdict(bool ack_ok)
{
    if (!s_x.done_pending)
    {
        /* Late callback for a session that has already gone -- a GAP disconnect
         * during the wait resets the context, and the ack's completion can still
         * arrive afterwards.  Nothing to report and nobody to report it to. */
        return;
    }
    s_x.done_pending = false;

    if (!ack_ok)
    {
        /* Worth naming: it means the phone got no TCP result and is relying
         * entirely on the notify below. */
        EBADGE_WARN("xfer: data-plane ack did not land; BLE verdict only");
    }

    /* Only now disarm our side.  Doing it before the ack settled would have
     * disarmed the connection the ack was staged against. */
    (void)ebadge_port_tcp_close();
    (void)ebadge_port_softap_stop();
    /* The last chunk ended exactly on the file's last byte, but the 8711 may
     * still push a stray slot -- same reason as tear_down_data_plane(). */
    jpgs_ingress_reset();
    ebfs_ingress_reset();

    if (s_x.done_ok)
    {
        emit_done(s_x.done_file_id);
    }
    else
    {
        emit_fail(s_x.done_reason, s_x.done_detail);
    }
    reset_ctx();
}

static void ack_settled_on_l2(void *arg)
{
    emit_held_verdict(arg != NULL);
}

/**
 * port_tcp's ack completion.  Fires on the transport thread or the system
 * workqueue, so it only marshals -- everything above touches session state.
 *
 * The bool travels as the arg pointer rather than through a static: post_call
 * copies nothing, and a static flag could be overwritten by a second settle
 * before the first hop ran.  Only two values are ever needed.
 */
static void ack_settled_from_driver(bool ok, void *user)
{
    (void)user;
    (void)ebadge_task_post_call(ack_settled_on_l2, ok ? (void *)1 : NULL);
}

/**
 * Fail the session, reporting the reason on both planes.
 *
 * The data plane gets a verdict (spec §5.3: status=failure plus the §2.6 reason,
 * which the 8711 turns into an EBXR), and BLE gets the 0x16 XFER_FAIL carrying
 * the same byte -- so the App sees one code whichever plane it is watching.
 *
 * The ack goes first and the 0x16 follows it, for the reason set out above
 * emit_held_verdict().  The session therefore does NOT end here: it parks in
 * COMPLETING until the ack settles, guarded by the tick's XS_ACK_WAIT_MS.
 *
 * @p detail must have static lifetime -- it is logged after this returns.  Every
 * caller passes a literal.
 */
static void fail_and_reset(uint8_t reason, const char *detail)
{
    /* Free the flash reservation and stop the ingress now: the verdict is
     * already decided, so any further inbound byte is waste.  The connection
     * itself stays armed for the ack -- tear_down_data_plane() would close it. */
    if (s_x.wp_handle > 0)
    {
        (void)ebadge_port_storage_wp_abort(s_x.wp_handle);
        s_x.wp_handle = 0;
    }
    jpgs_ingress_reset();
    ebfs_ingress_reset();

    s_x.state        = XFER_SESSION_COMPLETING;
    s_x.done_pending = true;
    s_x.done_ok      = false;
    s_x.done_reason  = reason;
    s_x.done_detail  = detail;
    s_x.deadline_ack = ebadge_task_now_ms() + XS_ACK_WAIT_MS;

    (void)ebadge_port_tcp_ack(false, reason, ack_settled_from_driver, NULL);
}

/**
 * Fail the session and CUT the stream, with no verdict on the data plane.
 *
 * For the cases where continuing to receive is pointless and the inbound bytes
 * are only costing time and flash writes -- chiefly a data-plane header that
 * contradicts the BLE offer, which no later byte can reconcile.  The distinction
 * from fail_and_reset() is deliberate and is the one the transport draws too
 * (AT+XFERSTOP vs AT+XFERACK): "stop sending" is a different request from "here
 * is the result", and a stream is often worth stopping before there is any
 * result to report.
 *
 * The App learns the reason from the 0x16 below, which is the plane it is
 * required to believe anyway.
 */
static void abort_and_reset(uint8_t reason, const char *detail)
{
    (void)ebadge_port_tcp_abort();

    tear_down_data_plane();
    emit_fail(reason, detail);
    reset_ctx();
}

/*----------------------------------------------------------------------------*
 *  Bring-up on ACCEPT: raise SoftAP + arm TCP listen
 *----------------------------------------------------------------------------*/
static void on_softap_joined_from_driver(void)
{
    /* Already on l2_task -- port_softap marshals the edge itself, because it
     * discovers it from an AT reply on the transport thread and has to re-check
     * the session is still alive after the hop anyway.  Calling
     * ebadge_task_post_call() again here would only add a second hop. */
    xfer_session_on_sta_joined();
}

static void on_tcp_data_from_driver(const uint8_t *data, uint16_t len)
{
    /* on driver thread; the impl of port_tcp is expected to allocate a
     * heap copy + post_call.  Here we only receive the "already on
     * l2_task" version through xfer_session_on_tcp_data(); this
     * function itself is the raw callback registered with port_tcp and
     * must therefore heap-copy + post.                                   */
    if (len == 0) { return; }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) { return; }
    memcpy(copy, data, len);
    /* Small trampoline: we cannot pack (ptr, len) into a single void*
     * unless we allocate a wrapper.  Use a 2-field struct.               */
    struct chunk { uint8_t *p; uint16_t n; } *c =
        (struct chunk *)malloc(sizeof(*c));
    if (!c) { free(copy); return; }
    c->p = copy; c->n = len;
    /* Trampoline fn defined below to unpack and free.                    */
    extern void xfer_data_trampoline(void *arg);
    (void)ebadge_task_post_call(xfer_data_trampoline, c);
}

void xfer_data_trampoline(void *arg)
{
    struct chunk { uint8_t *p; uint16_t n; } *c = arg;
    if (!c) { return; }
    xfer_session_on_tcp_data(c->p, c->n);
    free(c->p);
    free(c);
}

static void on_tcp_close_from_driver(ebadge_tcp_close_reason_t r)
{
    /* Pack the reason into arg (small enough); avoid heap.               */
    (void)ebadge_task_post_call((ebadge_post_fn_t)xfer_session_on_tcp_close,
                                (void *)(uintptr_t)r);
}

/*----------------------------------------------------------------------------*
 *  Tick handler -- runs on l2_task every EBADGE_TICK_MS
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
    if (s_x.state == XFER_SESSION_IDLE)
    {
        return;
    }

    /* Overall session hard deadline.
     *
     * Skipped once a verdict is already out on the data plane and only its
     * completion is outstanding: failing the session then would emit a second,
     * contradictory BLE notify for a transfer whose outcome is already decided.
     * That wait has its own deadline below. */
    if (!s_x.done_pending &&
        (int32_t)(now_ms - s_x.deadline_overall) >= 0)
    {
        EBADGE_WARN("xfer: overall deadline hit");
        fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "overall timeout");
        return;
    }

    if (s_x.done_pending &&
        (int32_t)(now_ms - s_x.deadline_ack) >= 0)
    {
        /* The ack's completion never arrived.  Release the held verdict rather
         * than sit here: the App is waiting for it, and port_tcp has already
         * given up on the data plane by now either way. */
        EBADGE_WARN("xfer: ack completion lost, releasing BLE verdict");
        emit_held_verdict(false);
        return;
    }

    switch (s_x.state)
    {
    case XFER_SESSION_WAIT_CONFIRM:
        if ((int32_t)(now_ms - s_x.deadline_stage) >= 0)
        {
            EBADGE_WARN("xfer: user did not confirm in 30s");
            /* Spec §4.8: decision=TIMEOUT, reason=USER_TIMEOUT. */
            emit_decision(EB_DECISION_TIMEOUT, EB_XFER_ERR_USER_TIMEOUT);
            reset_ctx();
        }
        break;

    case XFER_SESSION_WAIT_STA:
        if ((int32_t)(now_ms - s_x.deadline_stage) >= 0)
        {
            EBADGE_WARN("xfer: STA did not join in 60s");
            fail_and_reset(EB_XFER_ERR_STA_TIMEOUT, "sta join timeout");
        }
        break;

    case XFER_SESSION_RECV:
        /* Idle watchdog: if no TCP data for XS_RECV_IDLE_MS, bail. */
        if ((int32_t)(now_ms - (s_x.last_data_ms + XS_RECV_IDLE_MS)) >= 0)
        {
            EBADGE_WARN("xfer: RECV idle, bail");
            fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "recv idle");
        }
        break;

    case XFER_SESSION_COMPLETING:
    case XFER_SESSION_IDLE:
    default:
        break;
    }
}

/*----------------------------------------------------------------------------*
 *  CRC32 (IEEE 802.3, poly 0xEDB88320) -- now shared with the stream path,
 *  see eb_crc32_update() in ebxf_frame.c.  Same algorithm, one copy.
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void xfer_session_init(void)
{
    reset_ctx();
    ebadge_task_set_tick(on_tick);
}

xfer_session_state_t xfer_session_state(void)
{
    return s_x.state;
}

uint32_t xfer_session_bytes_received(void) { return s_x.bytes_recv; }
uint32_t xfer_session_total_size(void)     { return s_x.size; }

/*----------------------------------------------------------------------------*/
void xfer_session_offer(const char *name, uint8_t file_type,
                        uint32_t size, uint32_t crc32, uint16_t replace_id)
{
    if (s_x.state != XFER_SESSION_IDLE)
    {
        EBADGE_WARN1("offer while busy (state=%d) -> XFER_FAIL BUSY", (int)s_x.state);
        emit_fail(EB_XFER_ERR_BUSY, "busy");
        return;
    }
    /* One radio, one SoftAP, one TCP 9000 -- a live preview stream owns all
     * three, so a file offer has to wait (spec §6.7).  Symmetric with the
     * check stream_session_offer() runs against us.                        */
    if (stream_session_state() != STREAM_SESSION_IDLE)
    {
        EBADGE_WARN1("offer while preview stream busy (state=%d) -> XFER_FAIL BUSY",
                     (int)stream_session_state());
        emit_fail(EB_XFER_ERR_BUSY, "preview stream busy");
        return;
    }

    /* Type check (spec §2.7): 0x00 UNKNOWN is illegal in an offer, and
     * anything above the enum's last member is a newer App than us.       */
    if (file_type < EB_FILE_TYPE_JPEG || file_type > EB_FILE_TYPE_MAX)
    {
        EBADGE_WARN1("offer: unsupported file_type=0x%02x -> XFER_FAIL FMT_UNSUPPORTED",
                     file_type);
        emit_fail(EB_XFER_ERR_FMT_UNSUPPORTED, "bad file_type");
        return;
    }

    /* A zero-length offer has nothing to receive; the EBXF header would also
     * fail its size cross-check later.  Reject up front as a verify error.  */
    if (size == 0)
    {
        EBADGE_WARN("offer: size=0 -> XFER_FAIL VERIFY");
        emit_fail(EB_XFER_ERR_VERIFY, "size=0");
        return;
    }

    /* Single-file cap imposed by the transport, not by our storage.
     *
     * The 8711's port-9000 entry accepts at most 2 MiB per file (SPI spec §6).
     * Answering immediately is better than accepting: an oversized offer would
     * otherwise bring the AP up, have the phone refused or truncated at the
     * upload port, and then fail on the idle deadline with IO_TIMEOUT -- a
     * misleading answer to a limit we can state up front. */
    if (size > XS_MAX_FILE_SIZE)
    {
        EBADGE_WARN2("offer: size=%u > %u -> XFER_FAIL TOO_LARGE",
                     (unsigned)size, (unsigned)XS_MAX_FILE_SIZE);
        emit_fail(EB_XFER_ERR_TOO_LARGE, "over 2MiB file cap");
        return;
    }

    /* Space check (spec §2.9): free must cover the payload plus the fixed
     * 4096-byte filesystem metadata margin.  Both sides are BYTES -- the
     * pre-alignment code compared a KB field against a byte count.
     *
     * st.free_bytes is now the largest *contiguous* free run (BF allocates
     * nothing else), so this rejects fragmented-but-nominally-roomy cases too.
     * The +EB_FS_MARGIN also covers BF's own block-align round-up, since
     * align_up(size, 4096) <= size + 4096 for the 4 KB NOR block. */
    ebadge_storage_stat_t st;
    if (ebadge_port_storage_stat(&st) == 0)
    {
        /* +XS_EBXF_STORED_LEN because the stored file carries the TCP framing in
         * front of the content, so that is what the reservation will ask for. */
        uint64_t need = (uint64_t)size + XS_EBXF_STORED_LEN + EB_FS_MARGIN;
        if (st.free_bytes < need)
        {
            EBADGE_WARN2("offer: no space, need=%u free=%u -> XFER_FAIL STORAGE_FULL",
                         (unsigned)need, (unsigned)st.free_bytes);
            emit_fail(EB_XFER_ERR_STORAGE_FULL, "storage full");
            return;
        }
    }

    /* Stash the offer.  Advance to WAIT_CONFIRM. */
    reset_ctx();
    if (name)
    {
        size_t n = strlen(name);
        if (n > EB_MAX_FILE_NAME) { n = EB_MAX_FILE_NAME; }
        memcpy(s_x.name, name, n);
        s_x.name[n] = '\0';
    }
    s_x.file_type       = file_type;
    s_x.size            = size;
    s_x.crc32_expected  = crc32;
    s_x.replace_id      = replace_id;
    s_x.state           = XFER_SESSION_WAIT_CONFIRM;

    uint32_t now = ebadge_task_now_ms();
    s_x.deadline_stage   = now + XS_WAIT_CONFIRM_MS;
    s_x.deadline_overall = now + XS_OVERALL_MS;

    EBADGE_LOG2("xfer: offer accepted for approval, size=%u type=%d",
                (unsigned)size, (int)file_type);

    /* V1.3 §4.7 removed the confirmation Overlay: "设备不弹窗，自动回复同意
     * 或拒绝".  Every reason to say no (busy / bad type / no space) has
     * already answered 0x16 and returned above, so reaching here IS the
     * accept.  Auto-answer now instead of parking in WAIT_CONFIRM waiting
     * for a UI call that no longer exists -- without this, V1.3 offers would
     * sit for 30s and then emit DECISION=TIMEOUT, i.e. every transfer fails.
     *
     * WAIT_CONFIRM and xfer_session_user_decision() are deliberately KEPT:
     * §5.7 still lists the state, the transition below runs through it, and
     * a product decision to reinstate a prompt only has to drop this call.  */
    EBADGE_LOG("xfer: WAIT_CONFIRM -> auto-accept (V1.3 §4.7, no user prompt)");
    xfer_session_user_decision(true);
}

void xfer_session_user_decision(bool accept)
{
    if (s_x.state != XFER_SESSION_WAIT_CONFIRM)
    {
        EBADGE_WARN1("user_decision in wrong state=%d", (int)s_x.state);
        return;
    }

    if (!accept)
    {
        EBADGE_LOG("xfer: user REJECT -> DECISION 0x00 reason=USER_REJECT");
        emit_decision(EB_DECISION_REJECT, EB_XFER_ERR_USER_REJECT);
        reset_ctx();
        return;
    }

    /* Notify decision first, then bring up the data plane. */
    EBADGE_LOG("xfer: user ACCEPT -> DECISION 0x01");
    emit_decision(EB_DECISION_ACCEPT, 0);

    /* Ask the radio what the AP IS -- do not invent it.  The 8711 owns the
     * SoftAP and its credentials cannot be set from this side, so a hardcoded
     * SSID here would send the phone looking for a network that does not
     * exist.  See ebadge_port_softap.h. */
    ebadge_softap_info_t info;
    uint16_t             tcp_port = 0;
    /* EBADGE_AP_PORT_FILE, because this session sends an EBXF header + body: the
     * 8711 only accepts that shape on FILE_PORT= (9000).  Naming the role rather
     * than taking "the port" is what stops this from being handed 5004, which
     * accepts bare JPEG and silently discards everything else. */
    int rc = ebadge_port_softap_start(&info, EBADGE_AP_PORT_FILE, &tcp_port,
                                      on_softap_joined_from_driver);
    if (rc != 0)
    {
        /* -EAGAIN means the credentials are not known yet, which on the wire is
         * still AP_START: §2.6 has no "ask me again" transfer reason, and the
         * App's recovery is the same either way -- retry the offer. */
        EBADGE_ERR1("xfer: softap_start FAIL (%d) -> XFER_FAIL AP_START", rc);
        fail_and_reset(EB_XFER_ERR_AP_START, "softap start");
        return;
    }
    s_x.tcp_port = tcp_port;

    /* The 8711 runs the TCP server itself, on the port it just reported, so
     * there is no socket to open on this side -- port_tcp only has to arm the
     * sink that the SPI JPGS/EBXF path feeds. */
    ebadge_tcp_listen_t lc =
    {
        .port     = tcp_port,
        .on_data  = on_tcp_data_from_driver,
        .on_close = on_tcp_close_from_driver,
    };
    if (ebadge_port_tcp_listen(&lc) != 0)
    {
        /* §2.6 has no separate listener code -- a failed listen leaves the
         * App with no reachable endpoint, which is exactly AP_START.       */
        EBADGE_ERR("xfer: tcp_listen FAIL -> XFER_FAIL AP_START");
        fail_and_reset(EB_XFER_ERR_AP_START, "tcp listen");
        return;
    }

    /* Emit AP_INFO so the App can associate. */
    /* Emit AP_INFO so the App can associate.  Only the "which session" is logged
     * here -- eb_emit_ap_info() prints the values it actually sends, and having
     * two logs of the same fields is how a reader ends up trusting the stale one. */
    EBADGE_LOG("xfer: emitting AP_INFO (file transfer -> the file port)");
    emit_ap_info(&info);

    s_x.state         = XFER_SESSION_WAIT_STA;
    s_x.deadline_stage = ebadge_task_now_ms() + XS_WAIT_STA_MS;
    EBADGE_LOG("xfer: WAIT_STA (60s)");
}

void xfer_session_on_sta_joined(void)
{
    if (s_x.state == XFER_SESSION_RECV)
    {
        /* Already receiving.  Reached when data arrived before the WLSTATE poll
         * reported the association and promoted us itself -- the poll's later
         * answer is then just redundant, not wrong, so it must not warn. */
        return;
    }
    if (s_x.state != XFER_SESSION_WAIT_STA)
    {
        EBADGE_WARN1("sta_joined in wrong state=%d", (int)s_x.state);
        return;
    }
    /* Open write session; ready to receive.
     *
     * Reserve the EBXF header alongside the file: the whole TCP packet is stored,
     * so the reservation has to cover the framing too or the last 40 bytes of a
     * file sized at the cap would hit FDB_NO_SPACE in wp_write.
     *
     * XS_EBXF_STORED_LEN, not EBXF_HDR_LEN directly, because this same function
     * serves the JPGS path where the 8711 strips the framing and there is no
     * header to store -- see the macro. */
    s_x.wp_handle = ebadge_port_storage_wp_begin(s_x.name,
                                                 s_x.size + XS_EBXF_STORED_LEN,
                                                 s_x.file_type);
    if (s_x.wp_handle < 0)
    {
        fail_and_reset(EB_XFER_ERR_STORAGE_FULL, "wp_begin");
        return;
    }
    s_x.state        = XFER_SESSION_RECV;
    s_x.last_data_ms = ebadge_task_now_ms();
    EBADGE_LOG("xfer: RECV started");
}

/**
 * Body bytes only -- no framing header.  Reached either from jpgs_ingress (which
 * gets bytes the 8711 already stripped) or from xfer_session_on_tcp_data() once
 * the EBXF header has been consumed.
 */
int xfer_session_on_payload(const uint8_t *data, uint16_t len)
{
    if (s_x.state != XFER_SESSION_RECV || data == NULL || len == 0)
    {
        return -1;
    }
    uint32_t now = ebadge_task_now_ms();
    s_x.last_data_ms = now;

    /* Append + accumulate CRC + progress notify. */
    int rc = ebadge_port_storage_wp_write(s_x.wp_handle, data, len);
    if (rc < 0)
    {
        fail_and_reset(EB_XFER_ERR_STORAGE_FULL, "wp_write");
        return -1;
    }
    s_x.crc32_running = eb_crc32_update(s_x.crc32_running, data, len);
    s_x.bytes_recv   += len;

    /* Progress throttling: fire on 5%-boundary crossings OR 200ms lapse.
     * The percentage is an emission trigger only -- the payload carries
     * absolute recv/total byte counts per spec §4.10.                  */
    uint8_t pct = (s_x.size ? (uint8_t)((uint64_t)s_x.bytes_recv * 100u
                                        / s_x.size) : 0);
    bool step_hit = pct >= (uint8_t)(s_x.last_progress_pct
                                     + XS_PROGRESS_STEP_PCT);
    bool time_hit = (int32_t)(now - (s_x.last_progress_ms
                                     + XS_PROGRESS_MS)) >= 0;
    if (step_hit || time_hit)
    {
        emit_progress(s_x.bytes_recv, s_x.size);
        s_x.last_progress_pct = pct;
        s_x.last_progress_ms  = now;
    }

    /* Done?  ">=" rather than "==": a sender that overruns the offered size is
     * caught by the CRC below, and stopping here keeps wp_write from being
     * called past the reserved capacity. */
    if (s_x.bytes_recv >= s_x.size)
    {
        s_x.state = XFER_SESSION_COMPLETING;

        /* Length first, then CRC.  An overrun is its own fault and worth naming
         * separately: a CRC mismatch says "the bytes are wrong", an overrun says
         * "the sender disagrees with the offer about how many there are", and the
         * second is the more useful thing to see in a log. */
        if (s_x.bytes_recv != s_x.size)
        {
            EBADGE_ERR2("xfer: length mismatch got=%u exp=%u",
                        (unsigned)s_x.bytes_recv, (unsigned)s_x.size);
            fail_and_reset(EB_XFER_ERR_VERIFY, "length");
            return -1;
        }

        if (s_x.crc32_running != s_x.crc32_expected)
        {
            EBADGE_ERR2("xfer: CRC mismatch got=0x%08x exp=0x%08x",
                        s_x.crc32_running, s_x.crc32_expected);
            fail_and_reset(EB_XFER_ERR_VERIFY, "crc32");
            return -1;
        }

        uint16_t file_id = 0;
        /* Commit only now, with the verified CRC: the compare above is what
         * makes this the "verification passed" path, and commit is the point of
         * no return.
         *
         * This is also the slowest thing between the last slot and the ack, and
         * the ack has a deadline -- the 8711 holds the connection for 120 s and
         * then closes it with no result at all (SPI spec §6.3).  Anything added
         * here eats into that window. */
        int crc_rc = ebadge_port_storage_wp_commit(s_x.wp_handle,
                                                   s_x.crc32_running,
                                                   s_x.ebxf_seen
                                                   ? EBXF_HDR_LEN : 0U,
                                                   &file_id);
        s_x.wp_handle = 0;
        if (crc_rc < 0)
        {
            fail_and_reset(EB_XFER_ERR_STORAGE_FULL, "wp_commit");
            return -1;
        }

        /* Stay in COMPLETING and hand the result to the data plane.  The BLE
         * 0x15 DONE is NOT sent here: it goes out from emit_held_verdict() once
         * the XFERACK has actually left, so the phone cannot be told over BLE that
         * the transfer finished while the upload it is still holding open has
         * heard nothing.  See the ordering note above emit_held_verdict().
         *
         * file_id is stashed because the notify is built later, on a callback that
         * has no access to this frame.  The data plane is NOT torn down here
         * either -- the ack is staged against that connection. */
        s_x.done_pending = true;
        s_x.done_ok      = true;
        s_x.done_reason  = 0U;
        s_x.done_detail  = NULL;
        s_x.done_file_id = file_id;
        s_x.deadline_ack = ebadge_task_now_ms() + XS_ACK_WAIT_MS;

        (void)ebadge_port_tcp_ack(true, 0, ack_settled_from_driver, NULL);
    }
    return 0;
}

/*----------------------------------------------------------------------------*
 *  Cross-plane identity check (spec §5.2 rule 3)
 *
 *  The BLE offer and the data-plane header both describe the file.  They are
 *  required to agree, and when they do not the honest reading is not "corrupt
 *  data" but "these are two different files" -- so no amount of further
 *  receiving can resolve it, and the right response is to stop rather than to
 *  keep writing bytes we have already decided to discard.
 *
 *  Hence abort_and_reset(): cut the stream, no verdict on the wire, reason over
 *  BLE.  Compare the CRC failure at the end of the file, which uses
 *  fail_and_reset() because by then there IS a result to report.
 *----------------------------------------------------------------------------*/
bool xfer_session_check_identity(uint32_t size, uint32_t crc32,
                                 uint8_t file_type, const char *name)
{
    if (s_x.state != XFER_SESSION_RECV)
    {
        /* Nothing offered this file.  Refusing rather than adopting it: a file
         * we have no offer for has no name we trust, no size to check against
         * and nowhere to put it. */
        EBADGE_WARN1("xfer: identity check with no session (state=%d)",
                     (int)s_x.state);
        return false;
    }

    if (size != s_x.size)
    {
        EBADGE_WARN2("xfer: size mismatch data=%u offer=%u",
                     (unsigned)size, (unsigned)s_x.size);
        abort_and_reset(EB_XFER_ERR_VERIFY, "size mismatch");
        return false;
    }
    if (crc32 != s_x.crc32_expected)
    {
        EBADGE_WARN2("xfer: crc32 mismatch data=0x%08x offer=0x%08x",
                     crc32, s_x.crc32_expected);
        abort_and_reset(EB_XFER_ERR_VERIFY, "crc mismatch");
        return false;
    }
    if (file_type != s_x.file_type)
    {
        /* The 8711 passes the type byte through without interpreting it (SPI
         * spec §6), so this comparison is the only place it is checked against
         * anything at all. */
        EBADGE_WARN2("xfer: file_type mismatch data=0x%02x offer=0x%02x",
                     (unsigned)file_type, (unsigned)s_x.file_type);
        abort_and_reset(EB_XFER_ERR_FMT_UNSUPPORTED, "type mismatch");
        return false;
    }

    /* Name is compared only when the data plane supplies one.  §5.2 requires it
     * to match, but a transport that does not carry a name (the 0x02 BLE path)
     * is not violating anything by omitting it -- and the offer's name is the one
     * we store under either way, so a missing name costs nothing.
     *
     * An empty string counts as absent: it cannot legitimately match the offer's
     * name, and treating "" as a mismatch would fail transfers over a field the
     * sender simply left blank. */
    if (name != NULL && name[0] != '\0' &&
        strncmp(name, s_x.name, sizeof(s_x.name) - 1U) != 0)
    {
        EBADGE_WARN2("xfer: name mismatch data=\"%s\" offer=\"%s\"",
                     name, s_x.name);
        abort_and_reset(EB_XFER_ERR_VERIFY, "name mismatch");
        return false;
    }

    return true;
}

int xfer_session_on_tcp_data(const uint8_t *data, uint16_t len)
{
    if (s_x.state != XFER_SESSION_RECV || data == NULL || len == 0)
    {
        return -1;
    }

    uint16_t consumed = 0;

    /* First-time-only: consume the 40B EBXF header before body bytes.
     *
     * Reassembles across deliveries -- see the ebxf_hdr comment in xfer_ctx_t.
     * Note that a partial header is NOT fed to the running CRC: the CRC covers
     * file bytes only, and the header is not part of the file. */
    if (!s_x.ebxf_seen)
    {
        /* Roll the idle deadline here too: on_payload() below is not reached
         * while only header bytes have arrived. */
        s_x.last_data_ms = ebadge_task_now_ms();

        uint16_t want = (uint16_t)(EBXF_HDR_LEN - s_x.ebxf_hdr_len);
        uint16_t take = (len < want) ? len : want;
        memcpy(s_x.ebxf_hdr + s_x.ebxf_hdr_len, data, take);
        s_x.ebxf_hdr_len = (uint8_t)(s_x.ebxf_hdr_len + take);
        consumed         = take;

        if (s_x.ebxf_hdr_len < EBXF_HDR_LEN)
        {
            /* Still short.  Returning here is safe: the RECV-idle deadline was
             * just rolled, so a sender that stops mid-header still times out
             * rather than wedging the session. */
            EBADGE_LOG2("xfer: EBXF header %d/%d bytes, waiting",
                        (int)s_x.ebxf_hdr_len, (int)EBXF_HDR_LEN);
            return 0;
        }

        ebxf_hdr_t hdr;
        if (ebxf_hdr_parse(s_x.ebxf_hdr, &hdr) != 0)
        {
            /* Unrecoverable: everything after this point is body payload, so
             * there is no byte pattern to resynchronise on.  Cut the stream --
             * a sender we cannot parse is not one to keep listening to. */
            EBADGE_ERR("xfer: bad EBXF magic/version");
            abort_and_reset(EB_XFER_ERR_VERIFY, "ebxf magic");
            return -1;
        }
        /* Spec §5.2 rule 3, via the shared check so this path and the EBFS one
         * cannot drift apart.  It tears the session down on mismatch, so there
         * is nothing to do here but stop. */
        if (!xfer_session_check_identity(hdr.file_size, hdr.crc32,
                                         hdr.file_type, hdr.file_name))
        {
            return -1;
        }
        EBADGE_LOG3("xfer: EBXF hdr ok, file=\"%s\" size=%u crc=0x%08x",
                    hdr.file_name, (unsigned)hdr.file_size, hdr.crc32);
        s_x.ebxf_seen = true;

        /* Store the header as well, so what lands on flash is the whole TCP
         * packet rather than just its body.
         *
         * It is written here, once the header has been validated -- not as it
         * arrived.  A header that fails the parse or the identity check belongs
         * to a transfer that is about to be cut, and writing it first would leave
         * those 40 bytes in a reservation that is then aborted.
         *
         * Deliberately NOT fed to the running CRC: that accumulator is compared
         * against the offer's whole-FILE CRC32, which the sender computed over
         * the content alone.  Nor is it counted in bytes_recv, which is measured
         * against the offered file size and drives the progress notify.  The
         * header's 40 bytes are accounted for once, in the reservation made at
         * wp_begin, and once more as the content_offset handed to wp_commit. */
        int hrc = ebadge_port_storage_wp_write(s_x.wp_handle, s_x.ebxf_hdr,
                                               EBXF_HDR_LEN);
        if (hrc < 0)
        {
            fail_and_reset(EB_XFER_ERR_STORAGE_FULL, "wp_write ebxf hdr");
            return -1;
        }
    }

    if (consumed < len)
    {
        return xfer_session_on_payload(data + consumed,
                                       (uint16_t)(len - consumed));
    }
    return 0;
}

void xfer_session_on_tcp_close(int reason)
{
    if (s_x.state == XFER_SESSION_IDLE ||
        s_x.state == XFER_SESSION_COMPLETING)
    {
        /* Expected close after commit -- nothing to do. */
        return;
    }
    EBADGE_WARN1("xfer: tcp closed mid-session reason=%d", reason);
    fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "tcp closed");
}

void xfer_session_abort(void)
{
    if (s_x.state == XFER_SESSION_IDLE)
    {
        return;
    }
    EBADGE_LOG("xfer: abort");
    /* Cut the stream before tearing down.  Without this the 8711 would keep
     * forwarding slots for a session that no longer exists -- they would be
     * dropped as unrouted, but only after each one has occupied the SPI link and
     * back-pressured a phone that is still uploading. */
    (void)ebadge_port_tcp_abort();
    tear_down_data_plane();
    /* No notify -- the BLE link is probably gone (we are called from
     * the disconnect hook).  A subsequent OFFER will start fresh.        */
    reset_ctx();
}
