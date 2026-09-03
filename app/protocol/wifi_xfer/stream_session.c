/**
 * @file    stream_session.c
 * @brief   State machine for one in-flight JPEG preview stream (spec §6).
 *
 * Timing (§6.5 plus one gap of our own):
 *
 *   WAIT_STA       60s   phone STA must associate with our SoftAP
 *   FIRST_DATA     10s   §6.5: STA associated -> TCP connected + first bytes
 *   FRAME_GAP       3s   §6.4: gap between frames; expiry ends the session
 *
 * §6.5 tabulates only the 10s "STA 关联成功 -> TCP连接" row.  port_tcp has no
 * connect callback -- only on_data / on_close -- so "connected" is observed as
 * "first byte arrived", which is a slightly stricter reading of the same
 * intent.  WAIT_STA reuses the §5.5 60s value; §6 does not restate it, and
 * without it a phone that never joins would hold the AP up forever.
 *
 * Driven by:
 *   - BLE handler        stream_session_offer()
 *   - port_softap cb     on_sta_joined
 *   - port_tcp cb        on_tcp_data / on_tcp_close
 *   - ebadge_task tick   the three timeouts above
 *
 * NOTHING here writes to storage.  A preview frame is decoded and drawn (or
 * dropped) and then gone -- see the frame sink in stream_session.h.  That is
 * the whole reason this file exists instead of a flag on xfer_session.
 */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "stream_session.h"
#include "stream_sink.h"       /* stream_sink_reset() on every teardown path  */
#include "xfer_session.h"
#include "xfer_notify.h"
#include "ebxs_frame.h"
#include "ebxf_frame.h"        /* EBXR ack + eb_crc32_update                 */
#include "jpgs_ingress.h"      /* the JPEG boundary scanner in front of us   */
#include "../wifi_ota/ota_session.h"  /* third claimant on the same radio    */

#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#include "../port/ebadge_port_antenna.h"
#include "../port/ebadge_port_softap.h"
#include "../port/ebadge_port_tcp.h"

#if defined(CONFIG_WIFI_8711)
#include "spi_at_master.h"     /* set_dump/get_dump -- see dumps_suspend()     */
#endif

/*----------------------------------------------------------------------------*
 *  Config
 *----------------------------------------------------------------------------*/
#define SS_WAIT_STA_MS        60000u
#define SS_FIRST_DATA_MS      10000u    /* §6.5                              */
#define SS_FRAME_GAP_MS        3000u    /* §6.4                              */

/* No SS_TCP_PORT: the port comes from ebadge_port_softap_start(), and it is the
 * same one the file session uses (9000).  See ebadge_port_softap.h.          */

/** Default frame rate offered back when the App asks for something we cannot
 *  serve.  Mid-window rather than the max: the point of negotiating down is
 *  to pick something sustainable.                                          */
#define SS_FPS_PREFERRED         15u

/*----------------------------------------------------------------------------*
 *  Session state  (l2_task-owned; no locking)
 *----------------------------------------------------------------------------*/
typedef struct
{
    stream_session_state_t state;

    /* offer parameters ------------------------------------------------- */
    char     name[EB_MAX_FILE_NAME + 1];
    uint8_t  file_type;
    uint8_t  fps;                  /* agreed rate, not necessarily requested */

    /* EBXS reassembly -------------------------------------------------- */
    uint8_t  hdr[EBXS_HDR_LEN];
    uint8_t  hdr_got;              /* 0..EBXS_HDR_LEN                        */
    bool     in_payload;
    uint32_t frame_size;
    uint32_t frame_got;
    uint32_t crc32_expected;
    uint32_t crc32_running;

    /* This frame is being thrown away, but its bytes are still being counted.
     *
     * Set by either size check (see frame_too_big()).  Counting has to continue
     * because `frame_size` is what tells us where the NEXT header starts -- a
     * declared-length protocol has no resync marker, so abandoning the count would
     * make the following header land mid-payload and fail on its magic.  So the
     * frame is skipped, not the session: no CRC, no sink, framing intact. */
    bool     frame_skip;

    /* stats ----------------------------------------------------------- */
    uint32_t frames_ok;
    uint32_t frames_bad;

    /* timers (deadlines in ebadge_task_now_ms units) ------------------ */
    uint32_t deadline_stage;
    bool     saw_first_data;

    /* frame sink ------------------------------------------------------ */
    stream_frame_sink_t sink;
    void               *sink_user;
} stream_ctx_t;

static stream_ctx_t s_s;

/*----------------------------------------------------------------------------*
 *  Local helpers -- notify emission
 *
 *  Type numbers are per-command (spec §2.4): EB_TLV_SDEC_* is valid ONLY
 *  inside 0x09.  0x13 AP_INFO and 0x16 FAIL live in xfer_notify.c because the
 *  file-transfer session emits the very same bytes.
 *----------------------------------------------------------------------------*/

/**
 * 0x09 STREAM_DECISION (spec §4.6).
 * @param reason  optional; pass 0 to omit.
 * @param fps     optional; pass 0 to omit.  Required in practice when
 *                @p decision is NEGOTIATE, else the App has no new rate.
 */
static void emit_stream_decision(uint8_t decision, uint8_t reason, uint8_t fps)
{
    uint8_t  params[16];
    uint16_t off = 0;
    ebadge_tlv_put_u8(params, sizeof(params), &off,
                      EB_TLV_SDEC_DECISION, decision);
    if (reason != 0)
    {
        ebadge_tlv_put_u8(params, sizeof(params), &off,
                          EB_TLV_SDEC_REASON, reason);
    }
    if (fps != 0)
    {
        ebadge_tlv_put_u8(params, sizeof(params), &off,
                          EB_TLV_SDEC_FPS, fps);
    }
    (void)ebadge_l2_notify_send(EB_CMD_JPG_STREAM_DEC, params, off);
}

/*----------------------------------------------------------------------------*
 *  State transitions
 *----------------------------------------------------------------------------*/
static void reset_ctx(void)
{
    /* Keep the installed sink across sessions -- it is wired once by the UI
     * at startup, not per stream.                                          */
    stream_frame_sink_t sink = s_s.sink;
    void               *user = s_s.sink_user;
    memset(&s_s, 0, sizeof(s_s));
    s_s.state     = STREAM_SESSION_IDLE;
    s_s.sink      = sink;
    s_s.sink_user = user;
}

/*----------------------------------------------------------------------------*
 *  SPI hexdump suspension for the duration of a preview
 *
 *  A preview is a back-to-back stream of 16 KiB slots at up to 15 fps.  The
 *  1024 B threshold (SPI_AT_DUMP_MAX) already stops the bodies being printed,
 *  but every slot still costs one `len=N (not dumped, over 1024 B)` line, and
 *  those alone scroll the session's own logs away.  The outbound dump is worse
 *  than noisy: it runs on the transport thread with M2S still asserted, so its
 *  console time is subtracted from the peer's 1000 ms READY budget.
 *
 *  SAVE AND RESTORE, never force-on: `atm dump` is how someone turns these off
 *  while chasing something else, and a preview must not switch them back on
 *  behind their back.  The pair is idempotent because all four session-end
 *  paths funnel through tear_down_data_plane(), which may therefore run without
 *  a matching suspend.
 *----------------------------------------------------------------------------*/
#if defined(CONFIG_WIFI_8711)
static bool s_dump_saved;          /* a suspend is outstanding                */
static bool s_dump_tx_was;
static bool s_dump_rx_was;

static void dumps_suspend(void)
{
    if (s_dump_saved)
    {
        return;
    }
    spi_at_master_get_dump(&s_dump_tx_was, &s_dump_rx_was);
    s_dump_saved = true;
    if (s_dump_tx_was || s_dump_rx_was)
    {
        EBADGE_LOG("stream: SPI hexdump off for the preview (atm dump restores)");
        spi_at_master_set_dump(false, false);
    }
}

static void dumps_restore(void)
{
    if (!s_dump_saved)
    {
        return;
    }
    s_dump_saved = false;
    if (s_dump_tx_was || s_dump_rx_was)
    {
        spi_at_master_set_dump(s_dump_tx_was, s_dump_rx_was);
        EBADGE_LOG("stream: SPI hexdump restored");
    }
}
#else
static void dumps_suspend(void) { }
static void dumps_restore(void) { }
#endif /* CONFIG_WIFI_8711 */

static void tear_down_data_plane(void)
{
    dumps_restore();

    if (ebadge_port_softap_running())
    {
        (void)ebadge_port_softap_stop();
    }
    (void)ebadge_port_tcp_close();

    /* Both stages in front of the sink have per-frame state that must not carry
     * into the next session: jpgs_ingress may hold a partial frame in its PSRAM
     * staging buffer, and the sink may hold a transport buffer it acquired for a
     * frame that will now never complete.  The buffer is the one that matters --
     * without this the pool loses one buffer per interrupted session until
     * nothing is left and every later frame is dropped at acquire. */
    jpgs_ingress_reset();
    stream_sink_reset();

    /* Give the antenna back, and do it HERE rather than in each caller: this is the
     * one function all four session-end paths funnel through, and the release has to
     * happen before any of them emits a BLE notify.  fail_and_reset() is the case
     * that pins the position -- it stages the TCP cut BEFORE calling us and emits its
     * 0x16 AFTER, so sitting between the two is exactly right.
     *
     * What this does NOT wait for: the AT commands just staged (SKTDEL / the AP
     * release) drain asynchronously over SPI, so the FIN toward the phone may still
     * be unsent when the switch moves and would then never reach it.  Harmless here,
     * because nothing downstream depends on the phone SEEING the cut -- at_socket
     * clears its link table entry either way (see at_socket.c's del_done) and the
     * phone's own socket times out.  The alternative, holding the antenna until the
     * queue drains, would delay the 0x16 by seconds on the exact path where the App
     * is waiting to be told why the preview stopped. */
    ebadge_port_antenna_release_wifi("stream teardown");
}

/** Abnormal end: CUT the connection, then tell the App over BLE 0x16.
 *
 *  THAT ORDER IS LOAD-BEARING, and it is what a frame timeout exposed.  The App
 *  reacts to the 0x16 by offering again, and at_socket only forgets a seed link on
 *  a [$][SKT][DISCONNECTED] event -- which a phone that walked out of range never
 *  produces.  So a 0x16 sent while the old seed is still in the table races the
 *  next offer against it: ebadge_port_tcp_listen() adopts any seed it finds
 *  (at_socket_seed_of) and delivers an immediate joined edge, which drops the new
 *  session straight from the 60 s WAIT_STA into the 10 s FIRST_DATA window before
 *  the phone has even associated.  The second preview then dies of "no TCP data
 *  within 10s" on a radio that was never at fault.
 *
 *  Cutting first is what makes the re-offer land on a clean table.  This also puts
 *  the preview path level with xfer_session's abort_and_reset(): "stop sending" is
 *  a separate request from "here is the result", and a timeout wants both.
 */
static void fail_and_reset(uint8_t reason, const char *detail)
{
    /* BEFORE tear_down_data_plane(), not after: ebadge_port_tcp_close() clears the
     * very s_armed/s_seed that abort() needs to know what to delete, so the reverse
     * order would silently no-op.
     *
     * Harmless when there is nothing to cut -- an STA-join timeout has no
     * connection at all -- because abort() returns 0 early on s_seed < 0.
     *
     * Fire and forget, and it does not delay the 0x16: at_socket_del() is staged on
     * the AT queue and we do not wait for its OK.  What matters is the ORDER OF
     * STAGING, since the phone cannot re-offer faster than a BLE round trip.  One
     * known gap: if the AT queue is full the retry re-reads s_seed, finds the -1
     * that close() left, and abandons the delete with a warning -- the stale-seed
     * window then reopens.  Not defended against here, because the fix belongs in
     * listen(): it should verify a seed is still live before adopting it, rather
     * than trusting that this delete always got through. */
    (void)ebadge_port_tcp_abort();

    tear_down_data_plane();
    eb_emit_fail(reason, detail);
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* The report has gone out, so the hotspot has nothing left to serve.  See
     * xfer_session's emit_held_verdict() and ebadge_port_softap.h on why this is
     * SCHEDULED and not immediate: a phone that reacts to this 0x16 by re-offering
     * lands inside the settling window and pays no bring-up. */
    ebadge_port_softap_shutdown_when_idle();
#endif
    reset_ctx();
}

/** Normal end (§6.4: the App closes TCP when it leaves the preview).  No 0x16
 *  -- nothing failed -- and no 0x15 either: DONE carries a file_id and there
 *  is no file here.  The App already knows; it hung up.                    */
static void finish_and_reset(const char *why)
{
    EBADGE_LOG3("stream: session end (%s), frames ok=%u bad=%u", why,
                (unsigned)s_s.frames_ok, (unsigned)s_s.frames_bad);
    s_s.state = STREAM_SESSION_COMPLETING;
    tear_down_data_plane();
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* There is no report to wait behind on this path -- the App hung up, which is
     * how the preview ends -- but the delay is still wanted: leaving the preview and
     * immediately sending a file is a normal thing for a phone to do, and the window
     * is what lets that file transfer reuse this radio. */
    ebadge_port_softap_shutdown_when_idle();
#endif
    reset_ctx();
}

/*----------------------------------------------------------------------------*
 *  port_* callbacks -- arrive on driver threads, must marshal to l2_task
 *----------------------------------------------------------------------------*/
static void on_softap_joined_from_driver(void)
{
    /* Already on l2_task: the edge starts as an at_socket connect event on the
     * transport thread and port_tcp marshals it before port_softap ever sees it.
     * A post_call here would only add a second hop.
     *
     * Note what "joined" means on this stack: a phone opened the TCP connection.
     * The standard Wi-Fi command set emits no association events, so the socket is
     * the first observable event -- which for a preview is if anything the better
     * signal, since the sec.6.5 10 s "associated -> connected" budget it arms is a
     * budget this edge has already spent. */
    stream_session_on_sta_joined();
}

struct ss_chunk { uint8_t *p; uint16_t n; };

static void ss_data_trampoline(void *arg)
{
    struct ss_chunk *c = arg;
    if (!c) { return; }
    /* Through stream_session_on_tcp_data(), i.e. the §6.2 EBXS parser: the phone
     * puts a 14-byte header in front of every JPEG frame on this connection, so the
     * frame length arrives DECLARED rather than having to be discovered.
     *
     * That is what the size checks want -- an oversized frame is refused from its
     * header, before a payload byte is taken -- and it also brings back the
     * per-frame CRC32, which the scanner had no way to verify.
     *
     * jpgs_ingress (FF D8 .. FF D9 boundary detection, feeding
     * stream_session_on_frame_chunk()) is what used to be here, on the reading that
     * this port carried BARE JPEG.  It is kept, not deleted: it is the only thing
     * that can receive a stream with no framing at all, and its own size limit is
     * still in place.  Swapping this one call back is all it takes to return to it.
     *
     * If the two ever disagree on the wire the symptom is loud rather than subtle:
     * a bare-JPEG stream fails this parser's magic check on FF D8 and reports it
     * with the offending bytes -- see the log at the parse failure. */
    stream_session_on_tcp_data(c->p, c->n);
    free(c->p);
    free(c);
}

static void on_tcp_data_from_driver(const uint8_t *data, uint16_t len)
{
    /* Same shape as xfer_session's: heap-copy + post, because `data` belongs
     * to the transport and dies when this returns.                         */
    if (len == 0) { return; }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) { return; }
    memcpy(copy, data, len);
    struct ss_chunk *c = (struct ss_chunk *)malloc(sizeof(*c));
    if (!c) { free(copy); return; }
    c->p = copy; c->n = len;
    if (ebadge_task_post_call(ss_data_trampoline, c) != EBADGE_OK)
    {
        free(copy);
        free(c);
    }
}

static void on_tcp_close_from_driver(ebadge_tcp_close_reason_t r)
{
    (void)ebadge_task_post_call((ebadge_post_fn_t)stream_session_on_tcp_close,
                                (void *)(uintptr_t)r);
}

/*----------------------------------------------------------------------------*
 *  Tick handler -- on l2_task every EBADGE_TICK_MS
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
    if (s_s.state == STREAM_SESSION_IDLE)
    {
        return;
    }

    switch (s_s.state)
    {
    case STREAM_SESSION_WAIT_STA:
        if ((int32_t)(now_ms - s_s.deadline_stage) >= 0)
        {
            EBADGE_WARN("stream: STA did not join in 60s");
            fail_and_reset(EB_XFER_ERR_STA_TIMEOUT, "sta join timeout");
        }
        break;

    case STREAM_SESSION_RECV:
        if ((int32_t)(now_ms - s_s.deadline_stage) >= 0)
        {
            if (!s_s.saw_first_data)
            {
                /* §6.5: associated but never connected/sent within 10s. */
                EBADGE_WARN("stream: no TCP data within 10s of STA join");
                fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "tcp connect timeout");
            }
            else
            {
                /* §6.4 frame timeout.  A 3s gap while streaming means the App
                 * stopped feeding us; that is an abort, not a clean exit, so
                 * it does get a 0x16.                                       */
                EBADGE_WARN1("stream: frame gap > 3s (after %u ok frames)",
                             (unsigned)s_s.frames_ok);
                fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "frame timeout");
            }
        }
        break;

    case STREAM_SESSION_COMPLETING:
    case STREAM_SESSION_IDLE:
    default:
        break;
    }
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void stream_session_init(void)
{
    reset_ctx();
    ebadge_task_set_tick(on_tick);
}

stream_session_state_t stream_session_state(void) { return s_s.state; }
uint32_t stream_session_frames_ok(void)  { return s_s.frames_ok;  }
uint32_t stream_session_frames_bad(void) { return s_s.frames_bad; }
uint8_t  stream_session_fps(void)        { return s_s.fps;        }

void stream_session_set_frame_sink(stream_frame_sink_t sink, void *user)
{
    s_s.sink      = sink;
    s_s.sink_user = user;
}

/*----------------------------------------------------------------------------*/
void stream_session_offer(const char *name, uint8_t file_type, uint8_t fps)
{
    /* Busy in either direction.  The file transfer and the preview both want
     * the SoftAP and port 9000, so one radio == one session (§6.7).        */
    if (s_s.state != STREAM_SESSION_IDLE)
    {
        EBADGE_WARN1("stream: offer while streaming (state=%d) -> REJECT BUSY",
                     (int)s_s.state);
        emit_stream_decision(EB_DECISION_REJECT, EB_XFER_ERR_BUSY, 0);
        return;
    }
    if (xfer_session_state() != XFER_SESSION_IDLE)
    {
        EBADGE_WARN1("stream: offer while file xfer busy (state=%d) -> REJECT BUSY",
                     (int)xfer_session_state());
        emit_stream_decision(EB_DECISION_REJECT, EB_XFER_ERR_BUSY, 0);
        return;
    }
    /* The third claimant on the same radio: an 8711 firmware update.  Refuse
     * even in its push phase, when Wi-Fi is already down -- the module is being
     * rewritten, so a preview that raised the AP again would be talking to a
     * radio that is about to restart.                                       */
    if (ota_session_state() != OTA_SESSION_IDLE)
    {
        EBADGE_WARN1("stream: offer while an 8711 update is running (state=%d)"
                     " -> REJECT BUSY", (int)ota_session_state());
        emit_stream_decision(EB_DECISION_REJECT, EB_XFER_ERR_BUSY, 0);
        return;
    }

    /* §4.5: TLV_XFER_TYPE is required and must not be 0x00.  A preview is a
     * JPEG stream by definition -- a plain JPEG or a PNG here means the App
     * confused 0x08 with 0x10, so say FMT_UNSUPPORTED rather than silently
     * treating it as one enormous single frame.                            */
    if (file_type != EB_FILE_TYPE_JPEG_STREAM)
    {
        EBADGE_WARN1("stream: file_type=0x%02x is not JPEG_STREAM -> REJECT",
                     file_type);
        emit_stream_decision(EB_DECISION_REJECT,
                             EB_XFER_ERR_FMT_UNSUPPORTED, 0);
        return;
    }

    /* Frame-rate negotiation.  §4.6 gives us decision=2 + our own fps for
     * exactly this, so an out-of-window request is answered with a counter
     * offer instead of a rejection -- the App can still preview.           */
    uint8_t  agreed   = fps;
    uint8_t  decision = EB_DECISION_ACCEPT;
    if (fps < EB_STREAM_FPS_MIN || fps > EB_STREAM_FPS_MAX)
    {
        agreed   = SS_FPS_PREFERRED;
        decision = EB_STREAM_DEC_NEGOTIATE;
        EBADGE_LOG2("stream: fps=%d out of window -> negotiate %d",
                    (int)fps, (int)agreed);
    }

    reset_ctx();
    if (name)
    {
        size_t n = strlen(name);
        if (n > EB_MAX_FILE_NAME) { n = EB_MAX_FILE_NAME; }
        memcpy(s_s.name, name, n);
        s_s.name[n] = '\0';
    }
    s_s.file_type = file_type;
    s_s.fps       = agreed;

    EBADGE_LOG2("stream: offer accepted name=\"%s\" fps=%d",
                s_s.name, (int)agreed);

    /* Bring the data plane up BEFORE answering, so the 0x13 that follows the
     * decision is already backed by a live listener.  A failure here still
     * owes the App a 0x09 -- it is waiting for one -- so reject rather than
     * fall through to eb_emit_fail().
     *
     * The credentials come FROM the radio: the 8711 owns the SoftAP and its
     * SSID/password cannot be set from this side, so anything hardcoded here
     * would point the phone at a network that does not exist.  Reading them is
     * free -- the 8711 pushes its Wi-Fi state on the ~1 Hz POLL beat, so this
     * answers from a copy at most a second old without touching the wire.   */
    ebadge_softap_info_t info;
    uint16_t             tcp_port = 0;

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Ask for the radio before asking about it.  Same reasoning as the file
     * offer's, and it applies here for the same reason: under this arrangement the
     * AP is not up for the whole BLE connection -- a finished transfer or a finished
     * preview schedules it down -- so an offer arriving after one of those finds it
     * down as a matter of course rather than as a fault.
     *
     * It cannot ready the AP for THIS offer (a bring-up is seconds to tens of
     * seconds; see ebadge_port_softap.h).  It makes the AP_START rejection below
     * worth retrying, and it cancels a pending idle-down -- which is the case that
     * actually matters here, since a preview opened right after a file transfer
     * lands inside that settling window and then needs no bring-up at all.
     *
     * Below the validation, not above it: an offer that is rejected for a wrong
     * file_type or a busy peer must not leave a radio up with nothing scheduled to
     * bring it back down. */
    ebadge_port_softap_arm_on_l2("preview offer accepted");
#endif

    /* The same port the file session uses (9000).  There is no preview-specific
     * door any more, and no ambiguity from sharing one: this session and
     * xfer_session are mutually exclusive -- the check at the top of this function
     * refuses the offer with BUSY while a transfer runs, and xfer_session's does the
     * mirror -- so bare JPEG and an EBXF header are never on the wire together.
     * What tells them apart was never the port: ebadge_port_tcp forwards bytes to
     * whichever session armed the sink, which is us from the listen() below. */
    if (ebadge_port_softap_start(&info, &tcp_port,
                                 on_softap_joined_from_driver) != 0)
    {
        EBADGE_ERR("stream: softap_start FAIL -> REJECT AP_START");
        tear_down_data_plane();
        emit_stream_decision(EB_DECISION_REJECT, EB_XFER_ERR_AP_START, 0);
        reset_ctx();
        return;
    }
    ebadge_tcp_listen_t lc =
    {
        .port     = tcp_port,
        .on_data  = on_tcp_data_from_driver,
        .on_close = on_tcp_close_from_driver,
    };
    if (ebadge_port_tcp_listen(&lc) != 0)
    {
        EBADGE_ERR("stream: tcp_listen FAIL -> REJECT AP_START");
        tear_down_data_plane();
        emit_stream_decision(EB_DECISION_REJECT, EB_XFER_ERR_AP_START, 0);
        reset_ctx();
        return;
    }

    /* Everything that arrives from here on is preview payload, so silence the
     * transport dumps -- AFTER the AP/listener commands, whose replies are small,
     * under the threshold, and the ones worth reading when a preview fails to
     * start.  tear_down_data_plane() puts them back on every exit path. */
    dumps_suspend();

    /* 0x09 first, then 0x13 -- the App keys off the decision. */
    emit_stream_decision(decision, 0,
                         (decision == EB_STREAM_DEC_NEGOTIATE) ? agreed : 0);
    /* Which session is emitting, and nothing more: eb_emit_ap_info() prints the
     * credentials and port it actually puts on the wire.  Two logs of the same
     * values invite the reader to trust the earlier one. */
    EBADGE_LOG("stream: emitting AP_INFO (preview -> the stream port)");
    eb_emit_ap_info(&info, tcp_port);

    /* AP_INFO is out, so hand the antenna to Wi-Fi.  AFTER that notify and not one
     * line earlier: it carries the SSID, password and port the phone needs, and the
     * antenna moving first would mean the phone is told nothing and then asked to
     * join a hotspot it has never heard of.
     *
     * The claim is not delivery-confirmed, and cannot be: hmi_ctrl_service_notify()
     * queues into the GATT stack and returns, so the bytes are still in front of the
     * BLE controller here.  The gap is a couple of milliseconds of a link with no
     * other traffic on it -- against the tens of seconds the phone then spends
     * associating and connecting -- so a lost 0x13 shows up as a WAIT_STA timeout
     * (60 s, recoverable, the App re-offers), and closing the gap properly would mean
     * threading a tx-complete callback through for a case that ends in a retry
     * anyway.  ebadge_port_ble__on_send_data_cb() is where that hook would go if the
     * timeouts ever say it is needed. */
    ebadge_port_antenna_claim_wifi("preview data plane");

    s_s.state          = STREAM_SESSION_WAIT_STA;
    s_s.deadline_stage = ebadge_task_now_ms() + SS_WAIT_STA_MS;
    EBADGE_LOG("stream: WAIT_STA (60s)");

    /* Show the preview screen now, not on the first frame.
     *
     * Declared locally, following ebadge_port_storage.c's
     * notify_ui_new_resource(): this is a protocol-side reference to the designer
     * UI layer, and routing it through ViewMainFace_user.h would drag guidef.h
     * into app/protocol/ and invite the rest of the porting layer to reach into
     * the GUI too.  The function itself only posts a GUI message, so it is safe
     * to call from l2_task -- that indirection is why it exists.
     *
     * HERE rather than on the first committed frame, even though the screen shows
     * black until then: the App is about to join the SoftAP and start sending,
     * and everything between now and the first frame (association, TCP connect,
     * one frame encoded and delivered) is a second or more.  A view that switches
     * on the first frame would leave the wearer looking at the previous screen
     * through all of it, and would never switch at all for a preview that fails
     * to deliver -- which is exactly when someone wants to see that something was
     * attempted. */
    extern void ui_jump_streaming(void);
    ui_jump_streaming();
}

void stream_session_on_sta_joined(void)
{
    if (s_s.state != STREAM_SESSION_WAIT_STA)
    {
        EBADGE_WARN1("stream: sta_joined in wrong state=%d", (int)s_s.state);
        return;
    }
    /* No storage handle to open -- straight to receiving.  The stage deadline
     * now means "§6.5 first-data", and flips to the 3s frame gap once bytes
     * start arriving.                                                      */
    s_s.state          = STREAM_SESSION_RECV;
    s_s.saw_first_data = false;
    s_s.deadline_stage = ebadge_task_now_ms() + SS_FIRST_DATA_MS;
    EBADGE_LOG("stream: RECV, awaiting first frame (10s)");
}

/*----------------------------------------------------------------------------*
 *  Frame size limit -- two checks against one ceiling
 *
 *  THE CEILING.  stream_sink_max_frame_size() is the biggest size class the GUI
 *  configured in the display transport.  stp_acquire_free() refuses anything above
 *  it whatever the occupancy (allow_oversize_fit only escalates UPWARD through the
 *  classes), so a frame past it cannot be displayed -- not "might be dropped", but
 *  cannot, by construction.  It is therefore worth refusing early rather than
 *  carrying the bytes to the sink to be refused there.
 *
 *  WHY TWO CHECKS AND NOT ONE.  They catch different things and neither subsumes
 *  the other:
 *
 *    1. THE HEADER.  §6.2 declares the payload length up front, so an oversized
 *       frame is knowable before a single payload byte is taken.  This is the
 *       cheap one and the one that normally fires.
 *
 *    2. WHAT ACTUALLY ARRIVES.  The header is the sender's claim about itself, and
 *       the ceiling is not a constant: it reads 0 until the GUI has created the
 *       transport, and check 1 cannot refuse against an unknown limit.  A frame
 *       whose header passed while the answer was still 0 therefore has to be
 *       re-tested as it accumulates -- which is also the check that would survive
 *       a bug in the clamp arithmetic below.
 *
 *  A 0 ceiling means "not known yet", never "nothing fits": refusing every frame
 *  because the GUI has not finished booting would turn a transient into a dead
 *  preview.  Both checks skip themselves in that case and the sink reports the
 *  drop, exactly as it did before either check existed.
 *----------------------------------------------------------------------------*/

/**
 * @param  n     bytes to judge -- the header's declared size, or the running count.
 * @param  what  "header"/"payload", for the log; the two are worth telling apart.
 * @return true if the frame must be thrown away.  Sets frame_skip and counts it,
 *         once per frame -- a payload spanning many deliveries must not log twice.
 */
static bool frame_too_big(uint32_t n, const char *what)
{
    uint32_t max = stream_sink_max_frame_size();

    if (max == 0U || n <= max)
    {
        return s_s.frame_skip;
    }
    if (!s_s.frame_skip)
    {
        s_s.frame_skip = true;
        s_s.frames_bad++;
        EBADGE_WARN("stream: %s says %u bytes > %u max -- frame discarded, "
                    "nothing buffered", what, (unsigned)n, (unsigned)max);
    }
    return true;
}

/*----------------------------------------------------------------------------*
 *  One completed frame: verify CRC, count it, tell the sink nothing more.
 *----------------------------------------------------------------------------*/
static void frame_complete(void)
{
    if (s_s.frame_skip)
    {
        /* Already counted in frames_bad by frame_too_big(), and never CRC'd -- the
         * running value is 0 against a real expected one, so falling through would
         * report a CRC mismatch for a frame that was refused on its size. */
        s_s.hdr_got       = 0;
        s_s.in_payload    = false;
        s_s.frame_size    = 0;
        s_s.frame_got     = 0;
        s_s.crc32_running = 0;
        s_s.frame_skip    = false;
        return;
    }

    if (s_s.crc32_running == s_s.crc32_expected)
    {
        s_s.frames_ok++;
        /* Log the first frame and then every 30th -- at 24fps a per-frame
         * line would out-run the log ring and hide everything else.       */
        if (s_s.frames_ok == 1 || (s_s.frames_ok % 30u) == 0)
        {
            EBADGE_LOG3("stream: frame #%u ok, %u bytes (bad so far %u)",
                        (unsigned)s_s.frames_ok, (unsigned)s_s.frame_size,
                        (unsigned)s_s.frames_bad);
        }
    }
    else
    {
        s_s.frames_bad++;
        /* A corrupt frame is NOT fatal for a preview: drop it and keep the
         * session running, unlike the file path where a CRC mismatch fails
         * the whole transfer.  The next frame is independent.             */
        EBADGE_WARN2("stream: frame CRC mismatch got=0x%08x exp=0x%08x (dropped)",
                     (unsigned)s_s.crc32_running, (unsigned)s_s.crc32_expected);
    }

    /* Ready for the next header. */
    s_s.hdr_got       = 0;
    s_s.in_payload    = false;
    s_s.frame_size    = 0;
    s_s.frame_got     = 0;
    s_s.crc32_running = 0;
}

/**
 * One chunk of a preview frame whose framing is already resolved -- the live
 * path from jpgs_ingress.  See stream_session_on_frame_chunk() in the header.
 */
void stream_session_on_frame_chunk(const uint8_t *chunk, uint16_t len,
                                   uint32_t offset, uint32_t frame_size,
                                   bool is_last)
{
    if (s_s.state != STREAM_SESSION_RECV || chunk == NULL || len == 0)
    {
        return;
    }

    s_s.saw_first_data = true;
    s_s.deadline_stage = ebadge_task_now_ms() + SS_FRAME_GAP_MS;

    /* Mirror the geometry into the context so the introspection getters and the
     * mid-frame close log stay meaningful on this path too. */
    s_s.frame_size = frame_size;
    s_s.frame_got  = offset + len;
    s_s.in_payload = !is_last;

    if (s_s.sink)
    {
        s_s.sink(chunk, len, offset, frame_size, is_last, s_s.sink_user);
    }
    else if (offset == 0)
    {
        /* No sink -- only reachable if something called
         * stream_session_set_frame_sink(NULL) to go log-only, since
         * ebadge_task_init() installs stream_sink's.  Log the head of each frame
         * so a real JPEG can still be told from garbage without a decoder. */
        EBADGE_LOG_HEX("stream frame head", chunk, len);
    }

    if (is_last)
    {
        /* No CRC compare here, and nothing below verified one either -- that used
         * to be a per-slot CRC32 in jpgs_ingress, which was strictly finer-grained
         * than EBXS's per-frame one and made this omission safe.  The slots are
         * gone with the in-house firmware, so a frame that got this far is intact
         * only as far as SPI and TCP make it so.  Adding EBXS's per-frame CRC back
         * would not help: nothing on this path parses an EBXS header, because the
         * phone sends bare JPEG and the ingress layer finds frames by scanning for
         * FF D8 / FF D9.  A corrupt frame is caught by the decoder rejecting it,
         * which is the outcome a CRC here would have produced anyway.
         *
         * frames_bad stays at whatever the ingress layer dropped -- see
         * jpgs_ingress_frames_dropped(). */
        s_s.frames_ok++;
        if (s_s.frames_ok == 1 || (s_s.frames_ok % 30u) == 0)
        {
            EBADGE_LOG2("stream: frame #%u ok, %u bytes",
                        (unsigned)s_s.frames_ok, (unsigned)frame_size);
        }
        s_s.frame_got  = 0;
        s_s.frame_size = 0;
    }
}

void stream_session_on_tcp_data(const uint8_t *data, uint16_t len)
{
    if (s_s.state != STREAM_SESSION_RECV || data == NULL || len == 0)
    {
        return;
    }

    uint32_t now = ebadge_task_now_ms();
    s_s.saw_first_data = true;
    /* Every byte re-arms the §6.4 frame gap. */
    s_s.deadline_stage = now + SS_FRAME_GAP_MS;

    uint16_t off = 0;
    while (off < len)
    {
        if (!s_s.in_payload)
        {
            /* Accumulate the 14-byte header.  Unlike the file path -- which
             * gives up if its 40B header is split -- a stream MUST handle
             * this: headers land mid-buffer by construction once the first
             * frame's payload does not end on a delivery boundary.        */
            uint16_t want = (uint16_t)(EBXS_HDR_LEN - s_s.hdr_got);
            uint16_t take = (uint16_t)(len - off);
            if (take > want) { take = want; }
            memcpy(&s_s.hdr[s_s.hdr_got], data + off, take);
            s_s.hdr_got = (uint8_t)(s_s.hdr_got + take);
            off         = (uint16_t)(off + take);

            if (s_s.hdr_got < EBXS_HDR_LEN)
            {
                return;                     /* rest of the header next time  */
            }

            ebxs_hdr_t h;
            int rc = ebxs_hdr_parse(s_s.hdr, &h);
            if (rc != 0)
            {
                /* Cannot resync a raw byte stream once the framing is lost -- the
                 * next bytes are payload, not a header.  Bail out.
                 *
                 * The first four bytes go in the log because they answer the one
                 * question this failure raises: FF D8 .. means the sender is putting
                 * BARE JPEG on the wire and this parser is the wrong one for it (see
                 * ss_data_trampoline), while anything else means a genuinely
                 * corrupt or desynchronised header.  rc alone cannot tell them
                 * apart -- both arrive as -2. */
                EBADGE_ERR("stream: bad EBXS header (rc=%d) hdr=%02x %02x %02x %02x"
                           ", abort", rc,
                           s_s.hdr[0], s_s.hdr[1], s_s.hdr[2], s_s.hdr[3]);
                fail_and_reset(EB_XFER_ERR_VERIFY, "ebxs header");
                return;
            }
            if (h.file_type != EB_FILE_TYPE_JPEG_STREAM)
            {
                EBADGE_ERR1("stream: EBXS file_type=0x%02x unexpected, abort",
                            h.file_type);
                fail_and_reset(EB_XFER_ERR_FMT_UNSUPPORTED, "ebxs type");
                return;
            }
            s_s.frame_size     = h.frame_size;
            s_s.crc32_expected = h.crc32;
            s_s.frame_got      = 0;
            s_s.crc32_running  = 0;
            s_s.in_payload     = true;
            s_s.frame_skip     = false;

            /* CHECK 1 -- the declared size, before a single payload byte is taken.
             * Not fail_and_reset(): the header states exactly how many bytes to
             * throw away, so one oversized frame costs one frame.  ebxs_hdr_parse()
             * has its own EBXS_MAX_FRAME_BYTES cap above, but that is a sanity
             * bound on a corrupt length field (512 KB) and is not the display
             * ceiling -- a 200 KB frame passes it and still cannot be shown. */
            (void)frame_too_big(h.frame_size, "header");
            continue;                       /* payload may be in this buffer */
        }

        /* Payload bytes for the current frame. */
        uint32_t want32 = s_s.frame_size - s_s.frame_got;
        uint16_t take   = (uint16_t)(len - off);
        if ((uint32_t)take > want32) { take = (uint16_t)want32; }

        s_s.frame_got += take;

        /* CHECK 2 -- what has actually arrived.  Re-read every delivery, not once:
         * the ceiling is 0 until the GUI creates the transport, so a frame whose
         * header was judged against "unknown" gets caught here instead.  Skipping
         * mid-frame is safe because the count above already advanced -- framing
         * stays in sync whatever this decides. */
        bool skip = frame_too_big(s_s.frame_got, "payload");

        if (!skip)
        {
            s_s.crc32_running = eb_crc32_update(s_s.crc32_running,
                                                data + off, take);
        }

        bool is_last = (s_s.frame_got >= s_s.frame_size);
        if (skip)
        {
            /* Nothing to the sink -- not even the slice that crossed the limit, and
             * not the ones before it either.  The sink is stateless between frames
             * (it acquires on offset 0), so a frame whose head it never saw simply
             * never existed as far as the display is concerned. */
            off = (uint16_t)(off + take);
            if (is_last)
            {
                frame_complete();
            }
            continue;
        }

        if (s_s.sink)
        {
            s_s.sink(data + off, take,
                     s_s.frame_got - take, s_s.frame_size,
                     is_last, s_s.sink_user);
        }
        else if (s_s.frame_got == take)
        {
            /* No sink: log the JPEG SOI of each frame's head so bring-up can tell
             * real frames from garbage without a decoder.  Only reachable via an
             * explicit set_frame_sink(NULL) -- ebadge_task_init() installs
             * stream_sink's, which feeds the display. */
            EBADGE_LOG_HEX("stream frame head", data + off, take);
        }
        off = (uint16_t)(off + take);

        if (is_last)
        {
            frame_complete();
        }
    }
}

void stream_session_on_tcp_close(int reason)
{
    if (s_s.state == STREAM_SESSION_IDLE ||
        s_s.state == STREAM_SESSION_COMPLETING)
    {
        return;
    }
    /* §6.4: closing TCP is how the App leaves the preview.  Mid-frame is the
     * common case (it hangs up between renders, not on a frame boundary), so
     * a partial frame here is not an error -- just drop it.               */
    if (s_s.in_payload && s_s.frame_got < s_s.frame_size)
    {
        EBADGE_LOG2("stream: closed mid-frame (%u/%u bytes), dropping it",
                    (unsigned)s_s.frame_got, (unsigned)s_s.frame_size);
    }
    EBADGE_LOG1("stream: tcp close reason=%d", reason);
    finish_and_reset("tcp closed by app");
}

void stream_session_abort(void)
{
    if (s_s.state == STREAM_SESSION_IDLE)
    {
        return;
    }
    EBADGE_LOG("stream: abort");
    tear_down_data_plane();
    /* No notify -- abort comes from the disconnect hook, so the BLE link is
     * already gone.                                                       */
    reset_ctx();
}
