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
#include "xfer_session.h"
#include "xfer_notify.h"
#include "ebxs_frame.h"
#include "ebxf_frame.h"        /* EBXR ack + eb_crc32_update                 */

#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#include "../port/ebadge_port_softap.h"
#include "../port/ebadge_port_tcp.h"

/*----------------------------------------------------------------------------*
 *  Config
 *----------------------------------------------------------------------------*/
#define SS_WAIT_STA_MS        60000u
#define SS_FIRST_DATA_MS      10000u    /* §6.5                              */
#define SS_FRAME_GAP_MS        3000u    /* §6.4                              */

/* No SS_TCP_PORT: the 8711 runs the TCP server and reports the port itself
 * (5004 in practice).  See ebadge_port_softap.h.                            */

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

static void tear_down_data_plane(void)
{
    if (ebadge_port_softap_running())
    {
        (void)ebadge_port_softap_stop();
    }
    (void)ebadge_port_tcp_close();
}

/** Abnormal end: tell the App over BLE 0x16, then drop everything. */
static void fail_and_reset(uint8_t reason, const char *detail)
{
    tear_down_data_plane();
    eb_emit_fail(reason, detail);
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
    reset_ctx();
}

/*----------------------------------------------------------------------------*
 *  port_* callbacks -- arrive on driver threads, must marshal to l2_task
 *----------------------------------------------------------------------------*/
static void on_softap_joined_from_driver(void)
{
    /* Already on l2_task: port_softap marshals the edge itself (it learns of
     * the association from an AT reply on the transport thread).  A second
     * post_call here would only add a hop. */
    stream_session_on_sta_joined();
}

struct ss_chunk { uint8_t *p; uint16_t n; };

static void ss_data_trampoline(void *arg)
{
    struct ss_chunk *c = arg;
    if (!c) { return; }
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
     * would point the phone at a network that does not exist.               */
    ebadge_softap_info_t info;
    uint16_t             tcp_port = 0;
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

    /* 0x09 first, then 0x13 -- the App keys off the decision. */
    emit_stream_decision(decision, 0,
                         (decision == EB_STREAM_DEC_NEGOTIATE) ? agreed : 0);
    EBADGE_LOG2("stream: -> AP_INFO ssid=\"%s\" tcp_port=%d",
                info.ssid, (int)tcp_port);
    eb_emit_ap_info(&info, tcp_port);

    s_s.state          = STREAM_SESSION_WAIT_STA;
    s_s.deadline_stage = ebadge_task_now_ms() + SS_WAIT_STA_MS;
    EBADGE_LOG("stream: WAIT_STA (60s)");
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
 *  One completed frame: verify CRC, count it, tell the sink nothing more.
 *----------------------------------------------------------------------------*/
static void frame_complete(void)
{
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
        /* No sink installed yet -- same bring-up aid as the EBXS path: log the
         * head of each frame so a real JPEG can be told from garbage without a
         * decoder.  TODO(app): stream_session_set_frame_sink().            */
        EBADGE_LOG_HEX("stream frame head", chunk, len);
    }

    if (is_last)
    {
        /* No CRC compare here: jpgs_ingress verified a CRC32 per slot, which is
         * strictly finer-grained than EBXS's per-frame one, so a frame that got
         * this far is intact.  frames_bad stays at whatever the ingress layer
         * dropped -- see jpgs_ingress_frames_dropped(). */
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
                /* Cannot resync a raw byte stream once the framing is lost --
                 * the next bytes are payload, not a header.  Bail out.     */
                EBADGE_ERR1("stream: bad EBXS header (rc=%d), abort", rc);
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
            continue;                       /* payload may be in this buffer */
        }

        /* Payload bytes for the current frame. */
        uint32_t want32 = s_s.frame_size - s_s.frame_got;
        uint16_t take   = (uint16_t)(len - off);
        if ((uint32_t)take > want32) { take = (uint16_t)want32; }

        s_s.crc32_running = eb_crc32_update(s_s.crc32_running, data + off, take);
        s_s.frame_got    += take;

        bool is_last = (s_s.frame_got >= s_s.frame_size);
        if (s_s.sink)
        {
            s_s.sink(data + off, take,
                     s_s.frame_got - take, s_s.frame_size,
                     is_last, s_s.sink_user);
        }
        else if (s_s.frame_got == take)
        {
            /* No sink installed yet: log the JPEG SOI of each frame's head so
             * bring-up can tell real frames from garbage without a decoder.
             * TODO(app): install a sink that feeds the JPEG decoder / display
             * (stream_session_set_frame_sink) -- until then frames are
             * counted, CRC-checked, and discarded.                        */
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
