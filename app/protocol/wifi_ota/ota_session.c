/**
 * @file    ota_session.c
 * @brief   State machine for one 8711 firmware update.  See ota_session.h for the
 *          wire messages and for what the CRC32 does and does not protect.
 *
 * Deadlines (mirroring xfer_session's, and for the same reasons):
 *
 *   WAIT_STA      60s   phone must open the TCP connection on our SoftAP
 *   RECV         120s   idle-in-transfer watchdog (no data seen)
 *   OVERALL      300s   OFFER to last received byte -- see OS_OVERALL_MS
 *   ACK WAIT      30s   backstop for a lost EBXR completion, not a budget
 *   PUSH STALL   150s   backstop for a lost push completion, not a budget
 *
 * Driven by:
 *   - the 0xE0 handler (ota_session_offer)
 *   - port_softap's joined callback (already on l2_task; see the note at it)
 *   - port_tcp's on_data / on_close / ack completion
 *   - port_ota's completion, which port_ota has already marshalled onto l2_task
 *   - the ebadge_task tick (~100 ms), for the five deadlines above
 *
 * ---------------------------------------------------------------------------
 * THE TWO PHASES DO NOT OVERLAP, AND THAT IS STRUCTURAL
 * ---------------------------------------------------------------------------
 * Receiving needs the antenna pointed at Wi-Fi and the AT link free for socket
 * traffic.  Pushing needs the AT link for one long transparent-mode command and
 * does not need the antenna at all -- the SPI link to the 8711 is a set of traces,
 * not RF.  So the sequence is: receive the whole image, verify it, answer the phone
 * on the data plane, tear the Wi-Fi plane down, give the antenna back to BLE, push.
 *
 * The pleasant consequence is that BLE is on air for the entire push, which is the
 * slow part, so the App keeps its control link and gets the final verdict.  See
 * ota_cache.h for why a pass-through that avoided the buffering could not work.
 *
 * ---------------------------------------------------------------------------
 * WHY THE PUSH WAITS FOR THE DATA-PLANE ACK
 * ---------------------------------------------------------------------------
 * Same ordering rule as xfer_session, and the same reason -- there it is the flash
 * commit that waits, here it is the push.  The phone is holding a TCP connection
 * open for a result; the EBXR that answers it is a queued AT transaction on the very
 * link the push is about to occupy for tens of seconds.  Starting the push first
 * would put that whole stall in front of the answer.
 *
 * It is honest, not optimistic: the ack says "the bytes you sent arrived intact",
 * which the CRC has just settled.  Whether the module ACCEPTS them is a separate
 * question, and it is answered on BLE by 0xE5 / 0xE6 -- the plane the App is
 * required to believe.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>              /* snprintf, for the 0xE6 detail string */
#include <stdbool.h>
#include <errno.h>

#include "ota_session.h"
#include "ota_cache.h"

#include "../wifi_xfer/xfer_session.h"
#include "../wifi_xfer/stream_session.h"
#include "../wifi_xfer/xfer_notify.h"
#include "../wifi_xfer/ebxf_frame.h"

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

/* Whole-session cap, measured from the offer and STOPPED once the image is
 * verified.
 *
 * Larger than xfer_session's 180 s because the payload is larger: an OTA image is
 * a megabyte or more where a wallpaper is a couple of hundred kilobytes, and the
 * link is the same one.  Under-budgeting this would abandon transfers that were
 * progressing perfectly well.
 *
 * It does not cover the ack wait or the push, which are watchdogged separately
 * below -- a cap that spanned all three would have to be the sum of three very
 * different worst cases and would then be too loose to catch any of them. */
#define OS_OVERALL_MS        300000u

/* How long the held BLE verdict waits for the EBXR to settle before we give up on
 * the completion and carry on regardless.  Same figure and same reasoning as
 * XS_ACK_WAIT_MS in xfer_session.c: the AT link is single-flight and an EBXR queued
 * behind an AT+WLSTARTAP waits for a radio to come on air.
 *
 * Reaching it is a lost completion rather than a slow one -- port_tcp settles every
 * ack exactly once, including the paths where it could not stage the bytes.  Without
 * this the session would park in COMPLETING for good, with a perfectly good image in
 * PSRAM that never gets pushed. */
#define OS_ACK_WAIT_MS        30000u

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

    /* offer parameters ------------------------------------------------- */
    char     name[EBXF_NAME_LEN + 1];
    uint8_t  file_type;
    uint32_t size;
    uint32_t crc32_expected;

    /* data plane ------------------------------------------------------- */
    uint16_t tcp_port;            /* as reported by the AP, not chosen        */

    /* runtime ---------------------------------------------------------- */
    uint32_t bytes_recv;          /* payload only -- the EBXF header is not   */
    uint32_t crc32_running;
    bool     ebxf_seen;

    /* Partial EBXF header carried across deliveries.  The 40 bytes are not
     * guaranteed to arrive in one piece: they ride the same byte stream as the
     * image and the transport below chops that stream with no regard for where
     * the header ends, so a split header is routine rather than anomalous. */
    uint8_t  ebxf_hdr[EBXF_HDR_LEN];
    uint8_t  ebxf_hdr_len;

    /* Verdict held while the EBXR goes out ------------------------------
     *
     * Both planes report and the data plane goes first (see the ordering note in
     * the file header), so what happens next is decided on a callback with no
     * access to the frame that decided the outcome.  It is parked here.
     *
     * done_pending distinguishes "an ack is outstanding" from "COMPLETING for
     * some other reason", which is what lets the tick put a deadline on the wait
     * without arming one for every completion.
     *
     * push_pending is the success flavour of it: the image is verified and the
     * cache holds it, and the push starts when the ack settles.  A failure path
     * sets done_pending without it and gets 0xE6 at the same moment instead. */
    bool     done_pending;
    bool     push_pending;
    uint8_t  done_reason;         /* §2.6 code; only read when !push_pending  */
    const char *done_detail;      /* static lifetime -- read after the ack     */
    uint32_t deadline_ack;        /* only while done_pending                   */

    /* timers (deadlines in ebadge_task_now_ms units) ------------------- */
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

/* Defined next to the push it starts, but called from the ack-settled path. */
static void start_push(void);
/* Defined with the other data-plane callbacks, but handed to port_tcp from
 * fail_and_reset() above them. */
static void ack_settled_from_driver(bool ok, void *user);

/*----------------------------------------------------------------------------*
 *  Notify emission
 *
 *  PROGRESS / FAIL / DECISION share their builders with the file path -- same
 *  TLVs, different command byte, see the _on() variants in xfer_notify.h.  0xE5
 *  is built here because it does NOT share 0x15's TLVs: an update has no file id.
 *----------------------------------------------------------------------------*/
static void emit_decision(uint8_t decision, uint8_t reason)
{
    eb_emit_decision_on(EB_CMD_OTA_DECISION, decision, reason);
}

static void emit_progress(uint32_t recv, uint32_t total)
{
    eb_emit_progress_on(EB_CMD_OTA_PROGRESS, recv, total);
}

static void emit_fail(uint8_t reason, const char *detail)
{
    eb_emit_fail_on(EB_CMD_OTA_FAIL, reason, detail);
}

/** 0xE5 OTA_DONE -- SIZE and NAME keep their §4.11 numbers; FILE_ID is absent. */
static void emit_done(void)
{
    uint8_t  params[64];
    uint16_t off = 0;
    ebadge_tlv_put_u32(params, sizeof(params), &off,
                       EB_TLV_ODONE_SIZE, s_o.size);
    ebadge_tlv_put(params, sizeof(params), &off,
                   EB_TLV_ODONE_NAME,
                   (const uint8_t *)s_o.name,
                   (uint16_t)strlen(s_o.name));
    (void)ebadge_l2_notify_send(EB_CMD_OTA_DONE, params, off);
}

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

/** Refuse an offer before anything was promised: one 0xE6 and nothing else.
 *
 *  Same shape as the file path, which answers a refused 0x10 with 0x16 rather than
 *  with 0x11 REJECT -- REJECT is reserved for a decision a user made, and nothing
 *  here asks a user anything.
 *
 *  @p why is both the log text and the 0xE6 DETAIL TLV, so every caller keeps it
 *  within EB_MAX_MSG_STR: the builder truncates at 23 bytes, and a detail cut off
 *  mid-word is worse than a short one. */
static void refuse(uint8_t reason, const char *why)
{
    EBADGE_WARN1("ota: offer refused (%s)", why);
    emit_fail(reason, why);
}

/**
 * Fail an accepted session and report on BOTH planes, data plane first.
 *
 * The session does NOT end here: it parks in COMPLETING until the EBXR settles,
 * guarded by the tick's OS_ACK_WAIT_MS, and the 0xE6 goes out from
 * release_held_verdict().  For the failures where continuing to listen is
 * pointless and there is nothing to report yet, use abort_and_reset() instead.
 *
 * @p detail must have static lifetime -- it is read after this returns.
 */
static void fail_and_reset(uint8_t reason, const char *detail)
{
    /* Drop the buffered bytes now: this session will not be pushing them, and the
     * next one must not start on top of them.  The connection itself stays armed
     * for the ack -- tear_down_data_plane() would close it. */
    ota_cache_reset();

    s_o.state        = OTA_SESSION_COMPLETING;
    s_o.done_pending = true;
    s_o.push_pending = false;
    s_o.done_reason  = reason;
    s_o.done_detail  = detail;
    s_o.deadline_ack = ebadge_task_now_ms() + OS_ACK_WAIT_MS;

    /* The completion callback matters even here, where the outcome is already
     * decided: without it the 0xE6 would wait out OS_ACK_WAIT_MS on every failure
     * instead of going out as soon as the EBXR settles.  port_tcp settles it either
     * way -- including when it could not stage the bytes at all, which is the usual
     * case on this path because the connection is often already gone. */
    (void)ebadge_port_tcp_ack(false, reason, ack_settled_from_driver, NULL);
}

/**
 * Fail the session and CUT the stream, with no verdict on the data plane.
 *
 * For the cases where there is nothing to report on that plane or no point
 * listening further -- a header that contradicts the offer, which no later byte can
 * reconcile, and the bring-up failures that happen before any byte arrives.  The
 * distinction from fail_and_reset() is the one the transport draws too
 * (ebadge_port_tcp_abort() vs _ack()): "stop sending" is a different request from
 * "here is the result".
 */
static void abort_and_reset(uint8_t reason, const char *detail)
{
    (void)ebadge_port_tcp_abort();
    tear_down_data_plane();
    ota_cache_reset();

    emit_fail(reason, detail);
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Report first, then schedule the radio down -- same ordering rule as the file
     * path, for the same reason (see emit_held_verdict() in xfer_session.c): the
     * 0xE6 above has only just been queued, and an App that retries on it re-claims
     * the AP inside the settling window and pays no bring-up. */
    ebadge_port_softap_shutdown_when_idle();
#endif
    reset_ctx();
}

/** Fail after the Wi-Fi plane is already gone -- there is only BLE left to tell. */
static void push_fail_and_reset(uint8_t reason, const char *detail)
{
    emit_fail(reason, detail);
    reset_ctx();
}

/*----------------------------------------------------------------------------*
 *  The EBXR settled -- either start the push, or release the held 0xE6
 *----------------------------------------------------------------------------*/
static void release_held_verdict(bool ack_ok)
{
    if (!s_o.done_pending)
    {
        /* Late callback for a session that has already gone -- a GAP disconnect
         * during the wait resets the context, and the ack's completion can still
         * arrive afterwards.  Nothing to report and nobody to report it to. */
        return;
    }
    s_o.done_pending = false;

    if (!ack_ok)
    {
        /* Worth naming: the phone got no TCP result and is relying entirely on the
         * BLE notify.  Not a reason to withhold anything -- BLE is the plane that
         * counts, and on the success path the image is still good. */
        EBADGE_WARN("ota: data-plane ack did not land; BLE verdict only");
    }

    /* Only now disarm our side.  Doing it before the ack settled would have
     * disarmed the connection the ack was staged against.
     *
     * The antenna goes back to BLE here for both outcomes: a notify emitted with
     * the radio pointed at the 8711 is simply lost, and on the success path the
     * push that follows does not need the antenna at all. */
    tear_down_data_plane();

    if (!s_o.push_pending)
    {
        emit_fail(s_o.done_reason, s_o.done_detail);
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
        ebadge_port_softap_shutdown_when_idle();
#endif
        reset_ctx();
        return;
    }

    s_o.push_pending = false;
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Scheduled here rather than after the push: the hotspot has nothing left to
     * serve either way, and the push takes tens of seconds during which a beacon on
     * air would be pure cost.  Nothing in this session holds the radio any more --
     * tear_down_data_plane() released it above. */
    ebadge_port_softap_shutdown_when_idle();
#endif
    start_push();
}

static void ack_settled_on_l2(void *arg)
{
    release_held_verdict(arg != NULL);
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

/*----------------------------------------------------------------------------*
 *  Cross-plane identity check (the §5.2 rule 3 the file path applies)
 *----------------------------------------------------------------------------*/
static bool check_identity(const ebxf_hdr_t *hdr)
{
    if (hdr->file_size != s_o.size)
    {
        EBADGE_WARN2("ota: size mismatch data=%u offer=%u",
                     (unsigned)hdr->file_size, (unsigned)s_o.size);
        abort_and_reset(EB_XFER_ERR_VERIFY, "size mismatch");
        return false;
    }
    if (hdr->crc32 != s_o.crc32_expected)
    {
        EBADGE_WARN2("ota: crc32 mismatch data=0x%08x offer=0x%08x",
                     hdr->crc32, s_o.crc32_expected);
        abort_and_reset(EB_XFER_ERR_VERIFY, "crc mismatch");
        return false;
    }
    if (hdr->file_type != s_o.file_type)
    {
        EBADGE_WARN2("ota: file_type mismatch data=0x%02x offer=0x%02x",
                     (unsigned)hdr->file_type, (unsigned)s_o.file_type);
        abort_and_reset(EB_XFER_ERR_FMT_UNSUPPORTED, "type mismatch");
        return false;
    }
    /* Name compared only when the data plane supplies one, and an empty string
     * counts as absent -- it cannot legitimately match, and failing an update over
     * a field the sender left blank would be a poor trade.  Same rule as the file
     * path, which is where the offer's name comes from either way. */
    if (hdr->file_name[0] != '\0' && s_o.name[0] != '\0' &&
        strncmp(hdr->file_name, s_o.name, sizeof(s_o.name) - 1U) != 0)
    {
        EBADGE_WARN2("ota: name mismatch data=\"%s\" offer=\"%s\"",
                     hdr->file_name, s_o.name);
        abort_and_reset(EB_XFER_ERR_VERIFY, "name mismatch");
        return false;
    }
    return true;
}

/*----------------------------------------------------------------------------*
 *  Receive
 *----------------------------------------------------------------------------*/

/** Image bytes -- the EBXF header is already behind us. */
static int on_payload(const uint8_t *data, uint16_t len)
{
    uint32_t now = ebadge_task_now_ms();

    s_o.last_data_ms = now;

    int rc = ota_cache_append(data, len);
    if (rc < 0)
    {
        /* -ENOSPC here means the sender has gone past the size it declared; the
         * offer already refused anything the cache could not hold. */
        EBADGE_ERR2("ota: cache append failed rc=%d len=%d", rc, (int)len);
        fail_and_reset(rc == -ENOSPC ? EB_XFER_ERR_TOO_LARGE
                       : EB_XFER_ERR_IO_TIMEOUT,
                       "cache append");
        return -1;
    }
    s_o.crc32_running = eb_crc32_update(s_o.crc32_running, data, len);
    s_o.bytes_recv   += len;

    /* §4.10 throttle: a 5% boundary or 200 ms, whichever comes first.  The
     * percentage is the emission trigger only -- 0xE4 carries absolute counters. */
    uint8_t pct = (s_o.size != 0U)
                  ? (uint8_t)((uint64_t)s_o.bytes_recv * 100u / s_o.size)
                  : 0u;
    bool step_hit = pct >= (uint8_t)(s_o.last_progress_pct +
                                     OS_PROGRESS_STEP_PCT);
    bool time_hit = (int32_t)(now - (s_o.last_progress_ms +
                                     OS_PROGRESS_MS)) >= 0;
    if (step_hit || time_hit)
    {
        emit_progress(s_o.bytes_recv, s_o.size);
        s_o.last_progress_pct = pct;
        s_o.last_progress_ms  = now;
    }

    if (s_o.bytes_recv < s_o.size)
    {
        return 0;
    }

    /* --- the image is in.  Verify BEFORE anything reaches the module. ------ */

    /* Length first, then CRC.  An overrun is its own fault and worth naming
     * separately: a CRC mismatch says "the bytes are wrong", an overrun says "the
     * sender disagrees with the offer about how many there are", and the second is
     * the more useful thing to see in a log.  ">=" above rather than "==" is what
     * makes the distinction reachable. */
    if (s_o.bytes_recv != s_o.size || ota_cache_len() != s_o.size)
    {
        EBADGE_ERR2("ota: length mismatch got=%u exp=%u",
                    (unsigned)s_o.bytes_recv, (unsigned)s_o.size);
        fail_and_reset(EB_XFER_ERR_VERIFY, "length");
        return -1;
    }

    if (s_o.crc32_running != s_o.crc32_expected)
    {
        EBADGE_ERR2("ota: CRC mismatch got=0x%08x exp=0x%08x",
                    s_o.crc32_running, s_o.crc32_expected);
        /* Nothing to undo: the image only ever existed in PSRAM, and not one byte
         * of it has been offered to the module. */
        fail_and_reset(EB_XFER_ERR_VERIFY, "crc32");
        return -1;
    }

    /* Final progress notify, so the App's bar reaches 100% before the silence the
     * header warns about.  Unconditional: the throttle may well have skipped the
     * last chunk, and this is the one sample the App is sure to want. */
    emit_progress(s_o.bytes_recv, s_o.size);

    EBADGE_LOG1("ota: verified %u bytes -> acking the phone, then pushing",
                (unsigned)s_o.bytes_recv);

    s_o.state        = OTA_SESSION_COMPLETING;
    s_o.done_pending = true;
    s_o.push_pending = true;
    s_o.done_reason  = 0U;
    s_o.done_detail  = NULL;
    s_o.deadline_ack = ebadge_task_now_ms() + OS_ACK_WAIT_MS;

    (void)ebadge_port_tcp_ack(true, 0, ack_settled_from_driver, NULL);
    return 0;
}

/** A chunk off the TCP port, EBXF header still in front on the first call(s). */
static int on_tcp_data(const uint8_t *data, uint16_t len)
{
    if (s_o.state != OTA_SESSION_RECV || data == NULL || len == 0U)
    {
        return -1;
    }

    uint16_t consumed = 0;

    if (!s_o.ebxf_seen)
    {
        /* Roll the idle deadline here too: on_payload() is not reached while only
         * header bytes have arrived. */
        s_o.last_data_ms = ebadge_task_now_ms();

        uint16_t want = (uint16_t)(EBXF_HDR_LEN - s_o.ebxf_hdr_len);
        uint16_t take = (len < want) ? len : want;
        memcpy(s_o.ebxf_hdr + s_o.ebxf_hdr_len, data, take);
        s_o.ebxf_hdr_len = (uint8_t)(s_o.ebxf_hdr_len + take);
        consumed         = take;

        if (s_o.ebxf_hdr_len < EBXF_HDR_LEN)
        {
            EBADGE_LOG2("ota: EBXF header %d/%d bytes, waiting",
                        (int)s_o.ebxf_hdr_len, (int)EBXF_HDR_LEN);
            return 0;
        }

        ebxf_hdr_t hdr;
        if (ebxf_hdr_parse(s_o.ebxf_hdr, &hdr) != 0)
        {
            /* Unrecoverable: everything after this point is image payload, so there
             * is no byte pattern to resynchronise on. */
            EBADGE_ERR("ota: bad EBXF magic/version");
            abort_and_reset(EB_XFER_ERR_VERIFY, "ebxf magic");
            return -1;
        }
        if (!check_identity(&hdr))
        {
            /* Already torn down; nothing to do but stop. */
            return -1;
        }
        EBADGE_LOG3("ota: EBXF hdr ok, image=\"%s\" size=%u crc=0x%08x",
                    hdr.file_name, (unsigned)hdr.file_size, hdr.crc32);
        s_o.ebxf_seen = true;

        /* The header is NOT pushed to the module and NOT counted: AT+OTAUSER
         * declares the image length, and these 40 bytes are the transport's, not
         * the firmware's.  Nothing keeps them past this point. */
    }

    if (consumed < len)
    {
        return on_payload(data + consumed, (uint16_t)(len - consumed));
    }
    return 0;
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
    (void)on_tcp_data(c->p, c->n);
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
 * in the 0xE6 detail string, which is where a reader should look.
 */
static uint8_t reason_for_module_error(int error_no)
{
    /* Negative is not one of the module's numbers: at_client hands its own errno
     * up when the command never got far enough to have a reply, so the module may
     * not have seen the image at all.  VERIFY would be a lie in the App's face --
     * it says "your file is bad, do not resend it" -- when the truthful answer is
     * "the link under us failed, try again".  It is also now demonstrably wrong:
     * the CRC passed before the push started, so the bytes were good. */
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
        push_fail_and_reset(reason_for_module_error(error_no), detail);
        return;
    }

    /* The module has the image in its staging bank; only a restart makes it the
     * running firmware.  Fire and forget -- success is the module vanishing, so
     * there is no answer to wait for, and everything it was holding (SoftAP, TCP
     * server) has already been torn down. */
    int rc = ebadge_port_ota_restart();
    if (rc != 0)
    {
        /* The image is good and staged, but it is not running yet.  Reported as a
         * failure because that is the truthful answer to "did the update happen?",
         * and the recovery -- power-cycle, or retry -- is the user's. */
        EBADGE_ERR1("ota: image accepted but restart refused rc=%d", rc);
        push_fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "restart refused");
        return;
    }

    EBADGE_LOG1("ota: module took %u bytes and is restarting -> 0xE5 DONE",
                (unsigned)s_o.size);
    emit_done();
    reset_ctx();
}

/*----------------------------------------------------------------------------*
 *  COMPLETING -> PUSHING
 *----------------------------------------------------------------------------*/
/** The EBXR is out and the Wi-Fi plane is down.  Hand the image to the module. */
static void start_push(void)
{
    EBADGE_LOG1("ota: %u bytes verified -> pushing to the module over SPI AT",
                (unsigned)s_o.size);

    s_o.state         = OTA_SESSION_PUSHING;
    s_o.push_sent     = 0U;
    s_o.push_moved_ms = ebadge_task_now_ms();

    int rc = ebadge_port_ota_push(ota_cache_data(), ota_cache_len(), push_done);
    if (rc != 0)
    {
        EBADGE_ERR1("ota: push refused rc=%d", rc);
        ota_cache_reset();
        push_fail_and_reset(rc == -ENOTSUP ? EB_XFER_ERR_FMT_UNSUPPORTED
                            : EB_XFER_ERR_IO_TIMEOUT,
                            "push refused");
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
            push_fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "push stalled");
        }
        return;
    }

    /* Overall cap, skipped once a verdict is already out on the data plane and only
     * its completion is outstanding: failing the session then would emit a second,
     * contradictory BLE notify for an image whose fate is already decided.  That
     * wait has its own deadline below. */
    if (!s_o.done_pending &&
        (int32_t)(now_ms - s_o.deadline_overall) >= 0)
    {
        EBADGE_WARN("ota: overall deadline hit");
        fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "overall timeout");
        return;
    }

    if (s_o.done_pending &&
        (int32_t)(now_ms - s_o.deadline_ack) >= 0)
    {
        /* The ack's completion never arrived.  Release rather than sit here: on the
         * success path a good image would otherwise never be pushed, and on the
         * failure path the App is waiting for its reason. */
        EBADGE_WARN("ota: ack completion lost, releasing the held verdict");
        release_held_verdict(false);
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

    case OTA_SESSION_COMPLETING:
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

void ota_session_offer(const char *name, uint8_t file_type,
                       uint32_t size, uint32_t crc32)
{
    /* --- refusals, all answered with one 0xE6 and nothing else -------------- */
    if (s_o.state != OTA_SESSION_IDLE)
    {
        refuse(EB_XFER_ERR_BUSY, "update already running");
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
        refuse(EB_XFER_ERR_BUSY, "push still running");
        return;
    }
    /* One radio, one SoftAP, one TCP port -- the same mutual exclusion the other
     * two sessions apply to each other (spec §6.7). */
    if (xfer_session_state() != XFER_SESSION_IDLE)
    {
        refuse(EB_XFER_ERR_BUSY, "file transfer running");
        return;
    }
    if (stream_session_state() != STREAM_SESSION_IDLE)
    {
        refuse(EB_XFER_ERR_BUSY, "preview stream running");
        return;
    }
    /* The one field whose accepted VALUE differs from a file offer.  BIN is not a
     * formality: an image offered as JPEG is a mis-built request, and this is the
     * only place the type byte is checked against anything -- the 8711 passes it
     * through without interpreting it, and the EBXF cross-check only compares the
     * two planes with each other. */
    if (file_type != EB_FILE_TYPE_BIN)
    {
        EBADGE_WARN1("ota: file_type=0x%02x is not BIN", (unsigned)file_type);
        refuse(EB_XFER_ERR_FMT_UNSUPPORTED, "type must be bin");
        return;
    }
    if (size == 0U)
    {
        refuse(EB_XFER_ERR_VERIFY, "size=0");
        return;
    }
    if (size > ota_cache_capacity())
    {
        /* Stated up front rather than discovered part-way: accepting would bring
         * the AP up, run until ota_cache_append() refused a chunk, and then report
         * a limit we knew about before the transfer started. */
        EBADGE_WARN2("ota: size=%u > cache %u",
                     (unsigned)size, (unsigned)ota_cache_capacity());
        refuse(EB_XFER_ERR_TOO_LARGE, "image too large");
        return;
    }

    /* --- accepted ---------------------------------------------------------- */
    reset_ctx();
    if (name != NULL)
    {
        size_t n = strlen(name);
        if (n > EB_MAX_FILE_NAME) { n = EB_MAX_FILE_NAME; }
        memcpy(s_o.name, name, n);
        s_o.name[n] = '\0';
    }
    s_o.file_type      = file_type;
    s_o.size           = size;
    s_o.crc32_expected = crc32;
    ota_cache_reset();

    EBADGE_LOG2("ota: offer accepted, image=\"%s\" %u bytes", s_o.name,
                (unsigned)size);

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

    /* THE BRING-UP COMES BEFORE THE 0xE1, which is the one ordering difference
     * from the file path (it emits 0x11 ACCEPT first and then fails with 0x16 if
     * the radio will not come up).  Both work, and this way round an App gets
     * exactly one answer per offer: a bring-up failure is still a refusal, because
     * nothing has been promised yet.
     *
     * It costs nothing to do: the 8711 pushes its Wi-Fi state on the ~1 Hz poll
     * beat, so softap_start() reads a copy at most a second old and returns in
     * microseconds -- it does NOT wait for a bring-up. */
    int rc = ebadge_port_softap_start(&info, &tcp_port,
                                      on_softap_joined_from_driver);
    if (rc != 0)
    {
        /* -EAGAIN means the AP is down, still starting, or its state feed has
         * stopped.  All three clear on their own and the App's recovery is the same
         * -- retry the offer -- which is what AP_START says. */
        EBADGE_ERR1("ota: softap_start FAIL (%d)", rc);
        refuse(EB_XFER_ERR_AP_START, "softap not up");
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
        /* §2.6 has no separate listener code -- a failed listen leaves the App with
         * no reachable endpoint, which is exactly AP_START. */
        EBADGE_ERR("ota: tcp_listen FAIL");
        (void)ebadge_port_softap_stop();
        refuse(EB_XFER_ERR_AP_START, "tcp listener not armed");
        reset_ctx();
        return;
    }

    /* Accept, then the credentials -- the 0x11/0x13 pair the file path sends, with
     * 0xE1 in place of 0x11.  Unlike the earlier revision of this file, the 0x13 is
     * NOT the acceptance any more: 0xE1 says yes and 0x13 says where. */
    emit_decision(EB_DECISION_ACCEPT, 0);
    EBADGE_LOG("ota: emitting AP_INFO (firmware update -> the shared port)");
    eb_emit_ap_info(&info, s_o.tcp_port);

    /* Antenna to Wi-Fi only now.  Moving it earlier would send the 0xE1/0x13 with
     * the radio already pointed away from BLE, i.e. ask the phone to join a hotspot
     * it was never told about.  Released by tear_down_data_plane(), on every exit. */
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
    s_o.bytes_recv    = 0U;
    s_o.crc32_running = 0U;
    s_o.ebxf_seen     = false;
    s_o.ebxf_hdr_len  = 0U;
    s_o.state         = OTA_SESSION_RECV;
    s_o.last_data_ms  = ebadge_task_now_ms();
    EBADGE_LOG("ota: RECV started (EBXF header, then the image into PSRAM)");
}

void ota_session_on_tcp_close(int reason)
{
    if (s_o.state == OTA_SESSION_IDLE ||
        s_o.state == OTA_SESSION_COMPLETING ||
        s_o.state == OTA_SESSION_PUSHING)
    {
        /* Expected: the verdict is already decided and we close the connection
         * ourselves on the way into the push. */
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
         * restart it would have triggered is dropped with it.
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
