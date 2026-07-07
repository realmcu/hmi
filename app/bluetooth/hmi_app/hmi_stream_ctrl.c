/*
 * Independent BLE video stream service (decoupled from the shared L1/L2
 * protocol).  Stream data arrives on its own GATT characteristic (0xFFD4),
 * carries no L1 wrapper (reliability = BLE LL CRC24/ARQ + credit flow control
 * + KS_REPORT gap retransmission), is processed on a dedicated stream_task,
 * and the reassembled frame is committed to the (independent) STP pool.
 *
 * Migrated from app/l2_handlers/hmi_l2_cmd_stream.c; the frame-assembly /
 * gap / credit logic is unchanged, only the transport (RX queue + GATT
 * notify) is now stream-private.
 */

#include "hmi_stream_ctrl.h"
#include "hmi_stream_service.h"
#include "hmi_l2.h"            /* HMI_L2_CMD_STREAM / HMI_L2_KS_* / codec & ack codes */
#include "proto_log.h"
#include <os_timer.h>
#include <os_sched.h>
#include <os_msg.h>
#include <os_task.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* The video-frame pool and its STP transport are created and owned by the app
 * at GUI init (example_gui_stream.c: stp_instance_create()).  This BLE
 * producer only borrows that single shared instance through the getter below.
 * Declared here (instead of including the GUI header chain) so the BLE side
 * stays free of guidef.h / tlsf.h. */
extern stp_transport_t *gui_stream_transport_get(void);
extern stp_transport_t *app_stream_transport_get(void);

/* ---- Task / queue config ------------------------------------------------- */

#define STREAM_TASK_STACK_SIZE  2048
#define STREAM_TASK_PRIORITY    3
/* Stream RX queue depth: max in-flight KS_FRAME packets buffered before
 * stream_task drains them.  Independent from the generic l2Q.  Must be
 * > STREAM_INIT_CREDITS so the credit window can be advertised with headroom. */
#define STREAM_QUEUE_SIZE       72u

/* ---- Flow control config ------------------------------------------------- */

/* Initial credits = number of in-flight KS_FRAME packets the device can hold
 * = stream RX queue capacity (with headroom).  Drives the App's send window. */
#define STREAM_INIT_CREDITS     64u
/* Return KS_CREDIT every this many drained frames (~window/16).  CREDIT is a
 * fire-and-forget notify on a now-idle uplink, so a small batch is cheap and
 * keeps the App's window topped up smoothly. */
#define STREAM_CREDIT_BATCH     4u

/* ---- Gap detection & retransmission config ------------------------------- */

#define T_GAP_MS                40u     /* send KS_REPORT if gap persists this long after last new block */
#define T_RETX_MS               250u    /* discard frame if gap still open this long after KS_REPORT    */
#define MAX_GAPS                8u      /* max number of tracked byte-range gaps per frame              */

/* Max KS_REPORT value: session_id(1) + frame_seq(2) + gap_count(1) + gaps × 6 */
#define STREAM_REPORT_MAX_VAL   52u

/* Timer IDs for os_timer */
#define STREAM_TIMER_ID_GAP     1u
#define STREAM_TIMER_ID_RETX    2u

/* ---- RX transport state -------------------------------------------------- */

typedef struct
{
    uint8_t  *p_data;   /* malloc'd by stream_rx_sink, freed by stream_task */
    uint16_t  len;
} stream_msg_t;

static void    *s_stream_queue = NULL;
static void    *s_stream_task  = NULL;
static uint16_t s_conn_handle  = 0xFFFFu;  /* updated from RX / CCCD callbacks */

/* ---- STP transport (borrowed, not owned) --------------------------------- */

/* The stream service does NOT create its own transport -- it borrows the
 * single shared instance created by the app at GUI init
 * (example_gui_stream.c) via app_stream_transport_get(), so the producer
 * (this file) and the consumer (gui_stream widget) feed/drain the very same
 * transport.
 *
 * Resolved lazily and cached: this BLE service may run before the GUI init
 * that creates the transport, so the handle is fetched on first use.  NULL
 * until the app has created it. */
static stp_transport_t *s_tp = NULL;

static stp_transport_t *stream_tp(void)
{
    if (s_tp == NULL)
    {
        s_tp = gui_stream_transport_get();//app_stream_transport_get();//gui_stream_transport_get();
    }
    return s_tp;
}

stp_transport_t *hmi_l2_stream_get_tp(void)
{
    return stream_tp();
}

/* ---- State machine ------------------------------------------------------- */

typedef enum
{
    STREAM_STATE_IDLE = 0,
    STREAM_STATE_STREAMING,
} stream_state_t;

static stream_state_t s_state          = STREAM_STATE_IDLE;
static uint8_t        s_session_id     = 0;
static uint8_t        s_codec          = 0;
static uint16_t       s_credits_pending = 0; /* frames processed, credit not yet returned */

/* ---- Frame assembly state (valid in STREAMING) --------------------------- */

static uint16_t    s_frame_seq    = 0xFFFFu;
static uint32_t    s_accumulated  = 0;   /* length of the contiguous prefix [0, acc) received */
static uint32_t    s_total_len    = 0;
static uint32_t    s_high_water   = 0;   /* highest written byte offset (end-exclusive)        */
static stp_frame_t s_stp_frame    = {0};
static bool        s_buf_acquired = false;
static bool        s_skip_frame   = false;

/* ---- Gap list ------------------------------------------------------------ */

typedef struct
{
    uint32_t start;  /* first missing byte (inclusive) */
    uint32_t end;    /* first present byte (exclusive)  */
} gap_t;

static gap_t    s_gaps[MAX_GAPS];
static uint8_t  s_gap_count;
static uint32_t s_last_gap_tick;
static bool     s_gap_armed;
static bool     s_report_pending;
static uint32_t s_report_tick;

/* ---- Timers -------------------------------------------------------------- */

static void    *s_timer_gap  = NULL;
static void    *s_timer_retx = NULL;
static volatile bool s_timer_gap_fired  = false;
static volatile bool s_timer_retx_fired = false;

/* ---- Helpers ------------------------------------------------------------- */

static void stream_reset(void)
{
    s_state           = STREAM_STATE_IDLE;
    s_session_id      = 0;
    s_codec           = 0;
    s_credits_pending = 0;
    s_frame_seq       = 0xFFFFu;
    s_accumulated     = 0;
    s_total_len       = 0;
    s_high_water      = 0;
    memset(&s_stp_frame, 0, sizeof(s_stp_frame));
    s_buf_acquired = false;
    s_skip_frame   = false;

    s_gap_count      = 0;
    s_gap_armed      = false;
    s_report_pending = false;
    s_report_tick    = 0;
    s_last_gap_tick  = 0;

    if (s_timer_gap)
    {
        os_timer_stop(&s_timer_gap);
    }
    if (s_timer_retx)
    {
        os_timer_stop(&s_timer_retx);
    }
    s_timer_gap_fired  = false;
    s_timer_retx_fired = false;
}

/* Send a stream control message (KS_ACK/KS_CREDIT/KS_REPORT) to the peer.
 * Payload = L2 message [CMD_STREAM][ver][key][khdr][value], no L1 wrapper;
 * delivered fire-and-forget as a notify on 0xFFD5. */
static void stream_send(uint8_t key, const uint8_t *val, uint16_t val_len)
{
    if (val_len > STREAM_REPORT_MAX_VAL || s_conn_handle == 0xFFFFu)
    {
        return;
    }
    uint8_t  buf[5 + STREAM_REPORT_MAX_VAL];
    uint16_t pos = 0;

    buf[pos++] = HMI_L2_CMD_STREAM;
    buf[pos++] = 0x00u;
    buf[pos++] = key;
    buf[pos++] = (uint8_t)((val_len >> 8) & 0xFFu);
    buf[pos++] = (uint8_t)(val_len & 0xFFu);
    for (uint16_t i = 0; i < val_len; i++)
    {
        buf[pos++] = val[i];
    }

    hmi_stream_service_notify(s_conn_handle, buf, pos);
}

/* ---- Credit return ------------------------------------------------------- */

static void stream_maybe_send_credit(void)
{
    s_credits_pending++;
    if (s_credits_pending >= STREAM_CREDIT_BATCH)
    {
        uint8_t msg[3] =
        {
            s_session_id,
            (uint8_t)(s_credits_pending >> 8),
            (uint8_t)(s_credits_pending & 0xFFu),
        };
        stream_send(HMI_L2_KS_CREDIT, msg, sizeof(msg));
        s_credits_pending = 0;
    }
}

/* ---- Gap list helpers ---------------------------------------------------- */

static void gaps_clear(void)
{
    s_gap_count = 0;
}

/* Add [start, end) to the gap list, merging overlapping/adjacent entries. */
static void gaps_add(uint32_t start, uint32_t end)
{
    if (start >= end)
    {
        return;
    }

    uint8_t i;

    for (i = 0; i < s_gap_count; i++)
    {
        if (end < s_gaps[i].start)
        {
            break;
        }
        if (start > s_gaps[i].end)
        {
            continue;
        }
        if (start < s_gaps[i].start)
        {
            s_gaps[i].start = start;
        }
        if (end > s_gaps[i].end)
        {
            s_gaps[i].end = end;
        }
        while (i + 1 < s_gap_count && s_gaps[i].end >= s_gaps[i + 1].start)
        {
            if (s_gaps[i].end < s_gaps[i + 1].end)
            {
                s_gaps[i].end = s_gaps[i + 1].end;
            }
            memmove(&s_gaps[i + 1], &s_gaps[i + 2],
                    (s_gap_count - i - 2) * sizeof(gap_t));
            s_gap_count--;
        }
        return;
    }

    if (s_gap_count >= MAX_GAPS)
    {
        PROTO_LOG("STREAM gap list full, discard gap [%lu, %lu)",
                  (unsigned long)start, (unsigned long)end);
        return;
    }
    memmove(&s_gaps[i + 1], &s_gaps[i],
            (s_gap_count - i) * sizeof(gap_t));
    s_gaps[i].start = start;
    s_gaps[i].end   = end;
    s_gap_count++;
}

static void gaps_remove_at(uint8_t i)
{
    memmove(&s_gaps[i], &s_gaps[i + 1],
            (s_gap_count - i - 1) * sizeof(gap_t));
    s_gap_count--;
}

/* Return true if [a, b) overlaps any tracked gap. */
static bool gaps_overlaps(uint32_t a, uint32_t b)
{
    for (uint8_t i = 0; i < s_gap_count; i++)
    {
        if (a < s_gaps[i].end && b > s_gaps[i].start)
        {
            return true;
        }
    }
    return false;
}

/* Subtract the received range [a, b) from the gap list. */
static void gaps_subtract(uint32_t a, uint32_t b)
{
    if (a >= b)
    {
        return;
    }

    for (uint8_t i = 0; i < s_gap_count;)
    {
        uint32_t gs = s_gaps[i].start;
        uint32_t ge = s_gaps[i].end;

        if (b <= gs)
        {
            break;
        }
        if (a >= ge)
        {
            i++;
            continue;
        }

        if (a <= gs && b >= ge)
        {
            gaps_remove_at(i);
        }
        else if (a <= gs)
        {
            s_gaps[i].start = b;
            i++;
        }
        else if (b >= ge)
        {
            s_gaps[i].end = a;
            i++;
        }
        else
        {
            if (s_gap_count >= MAX_GAPS)
            {
                PROTO_LOG("STREAM gap split overflow at %lu", (unsigned long)a);
                s_gaps[i].end = a;
                i++;
            }
            else
            {
                memmove(&s_gaps[i + 1], &s_gaps[i],
                        (s_gap_count - i) * sizeof(gap_t));
                s_gaps[i].end       = a;
                s_gaps[i + 1].start = b;
                s_gap_count++;
                i += 2;
            }
        }
    }
}

/* ---- Timer callbacks ----------------------------------------------------- */

static void stream_gap_timer_cb(void *p_handle)
{
    (void)p_handle;
    s_timer_gap_fired = true;
}

static void stream_retx_timer_cb(void *p_handle)
{
    (void)p_handle;
    s_timer_retx_fired = true;
}

/* ---- KS_REPORT sending --------------------------------------------------- */

static void stream_send_report(void)
{
    uint8_t gap_count = (s_gap_count <= MAX_GAPS) ? s_gap_count : MAX_GAPS;
    uint8_t  val[4 + MAX_GAPS * 6];
    uint16_t pos = 0;

    val[pos++] = s_session_id;
    val[pos++] = (uint8_t)(s_frame_seq >> 8);
    val[pos++] = (uint8_t)(s_frame_seq & 0xFFu);
    val[pos++] = gap_count;

    for (uint8_t i = 0; i < gap_count; i++)
    {
        val[pos++] = (uint8_t)((s_gaps[i].start >> 16) & 0xFFu);
        val[pos++] = (uint8_t)((s_gaps[i].start >> 8) & 0xFFu);
        val[pos++] = (uint8_t)(s_gaps[i].start & 0xFFu);
        val[pos++] = (uint8_t)((s_gaps[i].end >> 16) & 0xFFu);
        val[pos++] = (uint8_t)((s_gaps[i].end >> 8) & 0xFFu);
        val[pos++] = (uint8_t)(s_gaps[i].end & 0xFFu);
    }

    stream_send(HMI_L2_KS_REPORT, val, pos);
    s_report_tick     = os_sys_time_get();
    s_report_pending  = (s_gap_count > 0);
    s_timer_gap_fired = false;

    if (s_gap_count > 0 && s_timer_retx)
    {
        os_timer_restart(&s_timer_retx, T_RETX_MS);
    }

    PROTO_LOG("STREAM REPORT seq=%d gaps=%d", s_frame_seq, gap_count);
}

/* Check timer flags and act; called before handling each KS_FRAME. */
static void stream_process_timers(void)
{
    if (s_state != STREAM_STATE_STREAMING || s_skip_frame)
    {
        return;
    }

    uint32_t now = os_sys_time_get();

    if (s_timer_retx_fired)
    {
        s_timer_retx_fired = false;
        if (s_gap_count > 0 || s_accumulated < s_total_len)
        {
            PROTO_LOG("STREAM RETX timeout seq=%d gaps=%d acc=%lu/%lu, discard",
                      s_frame_seq, s_gap_count,
                      (unsigned long)s_accumulated, (unsigned long)s_total_len);
            s_skip_frame   = true;
            s_buf_acquired = false;
            s_gap_count    = 0;
            s_report_pending = false;
            return;
        }
    }

    bool gap_timeout = s_timer_gap_fired;
    if (!gap_timeout && s_gap_count > 0)
    {
        if (s_timer_gap == NULL)
        {
            gap_timeout = ((now - s_last_gap_tick) >= T_GAP_MS);
        }
    }

    if (gap_timeout)
    {
        s_timer_gap_fired = false;
        if (s_gap_count > 0)
        {
            stream_send_report();
        }
    }
}

/* ---- KS_FRAME inner logic ------------------------------------------------ */

static void process_ks_frame(const uint8_t *val, uint16_t vl)
{
    uint16_t frame_seq   = ((uint16_t)val[0] << 8) | val[1];
    uint32_t byte_offset = ((uint32_t)val[2] << 16) | ((uint32_t)val[3] << 8) | val[4];
    uint32_t total_len   = ((uint32_t)val[5] << 16) | ((uint32_t)val[6] << 8) | val[7];
    const uint8_t *data  = val + 8;
    uint16_t       data_len = (uint16_t)(vl - 8);

    if (frame_seq != s_frame_seq && byte_offset != 0)
    {
        if (s_buf_acquired)
        {
            PROTO_LOG("STREAM FRAME discard incomplete seq=%d on new seq=%d",
                      s_frame_seq, frame_seq);
            s_buf_acquired = false;
        }
        s_frame_seq   = frame_seq;
        s_accumulated = 0;
        s_high_water  = 0;
        s_skip_frame  = true;
        gaps_clear();
        s_report_pending = false;
        return;
    }

    if (byte_offset == 0)
    {
        if (s_buf_acquired)
        {
            s_buf_acquired = false;
        }

        s_frame_seq   = frame_seq;
        s_total_len   = total_len;
        s_accumulated = 0;
        s_high_water  = 0;
        s_skip_frame  = false;

        if (!s_tp || !stp_acquire_free(s_tp, total_len, &s_stp_frame))
        {
            PROTO_LOG("STREAM FRAME no STP buffer, skip seq=%d total=%lu",
                      frame_seq, (unsigned long)total_len);
            s_skip_frame = true;
            return;
        }
        s_buf_acquired = true;

        gaps_clear();
        s_gap_armed      = false;
        s_report_pending = false;

        PROTO_LOG("STREAM FRAME new seq=%d total=%lu", frame_seq, (unsigned long)total_len);
    }

    if (s_skip_frame)
    {
        return;
    }

    if (s_total_len == 0 || total_len != s_total_len)
    {
        PROTO_LOG("STREAM FRAME total_len mismatch: frame=%lu block=%lu",
                  (unsigned long)s_total_len, (unsigned long)total_len);
        s_buf_acquired = false;
        s_skip_frame   = true;
        return;
    }

    if (byte_offset >= s_total_len)
    {
        PROTO_LOG("STREAM FRAME offset>=total: off=%lu total=%lu",
                  (unsigned long)byte_offset, (unsigned long)s_total_len);
        return;
    }

    if (byte_offset + data_len > s_total_len)
    {
        data_len = (uint16_t)(s_total_len - byte_offset);
    }
    if (byte_offset >= s_stp_frame.capacity)
    {
        PROTO_LOG("STREAM FRAME overflow: off=%lu >= capacity=%lu",
                  (unsigned long)byte_offset, (unsigned long)s_stp_frame.capacity);
        s_buf_acquired = false;
        s_skip_frame   = true;
        return;
    }
    if (byte_offset + data_len > s_stp_frame.capacity)
    {
        data_len = (uint16_t)(s_stp_frame.capacity - byte_offset);
    }

    uint32_t block_end = byte_offset + data_len;

    if (block_end <= s_high_water && !gaps_overlaps(byte_offset, block_end))
    {
        PROTO_LOG("STREAM FRAME dup seq=%d off=%lu len=%d acc=%lu",
                  frame_seq, (unsigned long)byte_offset, data_len,
                  (unsigned long)s_accumulated);
        return;
    }

    if (byte_offset > s_high_water)
    {
        gaps_add(s_high_water, byte_offset);
    }

    if (data_len > 0)
    {
        memcpy((uint8_t *)s_stp_frame.addr + byte_offset, data, data_len);
    }

    gaps_subtract(byte_offset, block_end);
    if (block_end > s_high_water)
    {
        s_high_water = block_end;
    }
    s_accumulated = (s_gap_count > 0) ? s_gaps[0].start : s_high_water;

    s_last_gap_tick = os_sys_time_get();

    if (s_gap_count > 0 && s_timer_gap)
    {
        os_timer_restart(&s_timer_gap, T_GAP_MS);
        s_gap_armed = true;
    }
    else
    {
        s_gap_armed = false;
    }

    PROTO_LOG("STREAM FRAME seq=%d off=%lu len=%d acc=%lu/%lu gaps=%d hw=%lu",
              frame_seq, (unsigned long)byte_offset, data_len,
              (unsigned long)s_accumulated, (unsigned long)s_total_len,
              s_gap_count, (unsigned long)s_high_water);

    if (s_gap_count == 0 && s_accumulated == s_total_len)
    {
        bool is_keyframe = (s_codec == HMI_L2_KS_CODEC_JPEG);
        stp_commit(s_tp, &s_stp_frame, s_total_len, is_keyframe);
        PROTO_LOG("STREAM FRAME complete seq=%d total=%lu committed",
                  s_frame_seq, (unsigned long)s_total_len);
        s_buf_acquired = false;
        s_accumulated  = 0;
        s_high_water   = 0;

        if (s_report_pending)
        {
            uint8_t ack_val[4];
            ack_val[0] = s_session_id;
            ack_val[1] = (uint8_t)(s_frame_seq >> 8);
            ack_val[2] = (uint8_t)(s_frame_seq & 0xFFu);
            ack_val[3] = 0; /* gap_count = 0 */
            stream_send(HMI_L2_KS_REPORT, ack_val, 4);
            s_report_pending = false;
        }

        if (s_timer_gap)
        {
            os_timer_stop(&s_timer_gap);
        }
        if (s_timer_retx)
        {
            os_timer_stop(&s_timer_retx);
        }
        s_gap_armed      = false;
        s_timer_gap_fired  = false;
        s_timer_retx_fired = false;
    }
}

/* ---- Per-key handling ---------------------------------------------------- */

static void stream_on_kv(uint8_t key, const uint8_t *val, uint16_t vl)
{
    switch (key)
    {
    case HMI_L2_KS_OPEN:
        {
            if (vl < 7)
            {
                PROTO_LOG("STREAM OPEN too short (%d)", vl);
                break;
            }

            uint8_t  sid    = val[0];
            uint8_t  codec  = val[1];
            uint16_t width  = (uint16_t)val[2] | ((uint16_t)val[3] << 8);
            uint16_t height = (uint16_t)val[4] | ((uint16_t)val[5] << 8);
            uint8_t  fps    = val[6];

            stream_reset();

            if (codec != HMI_L2_KS_CODEC_MSV1 && codec != HMI_L2_KS_CODEC_JPEG && codec != HMI_L2_KS_CODEC_H264)
            {
                uint8_t ack[2] = { sid, HMI_L2_KS_ACK_BAD_CODEC };
                stream_send(HMI_L2_KS_ACK, ack, sizeof(ack));
                PROTO_LOG("STREAM OPEN bad codec=0x%02x", codec);
                break;
            }

            /* Borrow the shared transport.  NULL means the app has not created
             * it yet (GUI init not done) -- tell the App we are busy so it
             * retries instead of streaming into a NULL pool. */
            if (stream_tp() == NULL)
            {
                uint8_t ack[2] = { sid, HMI_L2_KS_ACK_BUSY };
                stream_send(HMI_L2_KS_ACK, ack, sizeof(ack));
                PROTO_LOG("STREAM OPEN no transport (gui port not ready), busy");
                break;
            }

            if (s_timer_gap == NULL)
            {
                os_timer_create(&s_timer_gap, "stream_gap", STREAM_TIMER_ID_GAP,
                                T_GAP_MS, false, stream_gap_timer_cb);
            }
            if (s_timer_retx == NULL)
            {
                os_timer_create(&s_timer_retx, "stream_retx", STREAM_TIMER_ID_RETX,
                                T_RETX_MS, false, stream_retx_timer_cb);
            }

            s_session_id = sid;
            s_codec      = codec;
            s_state      = STREAM_STATE_STREAMING;

            uint8_t ack[4] =
            {
                sid, HMI_L2_KS_ACK_OK,
                (uint8_t)(STREAM_INIT_CREDITS >> 8),
                (uint8_t)(STREAM_INIT_CREDITS & 0xFFu),
            };
            stream_send(HMI_L2_KS_ACK, ack, sizeof(ack));

            PROTO_LOG("STREAM OPEN session=%d codec=%d w=%d h=%d fps=%d",
                      sid, codec, width, height, fps);
            break;
        }

    case HMI_L2_KS_FRAME:
        {
            if (s_state != STREAM_STATE_STREAMING || vl < 8)
            {
                PROTO_LOG("STREAM FRAME discard: state=%d vl=%d", s_state, vl);
                break;
            }

            stream_process_timers();
            process_ks_frame(val, vl);
            stream_maybe_send_credit();
            break;
        }

    case HMI_L2_KS_CLOSE:
        {
            if (s_state != STREAM_STATE_STREAMING)
            {
                PROTO_LOG("STREAM CLOSE ignored: not streaming");
                break;
            }
            uint8_t sid = (vl >= 1) ? val[0] : 0;
            PROTO_LOG("STREAM CLOSE session=%d", sid);
            stream_reset();
            break;
        }

    default:
        PROTO_LOG("STREAM unknown key=0x%02x", key);
        break;
    }
}

/* Parse one L2 message [CMD_STREAM][ver][KV...] and dispatch each KV. */
static void stream_dispatch(const uint8_t *msg, uint16_t len)
{
    if (len < 2 || msg[0] != HMI_L2_CMD_STREAM)
    {
        PROTO_LOG("STREAM dispatch: bad msg len=%d", len);
        return;
    }

    uint16_t pos = 2;   /* skip CMD + ver */
    while (pos + 3 <= len)
    {
        uint8_t  key  = msg[pos];
        uint16_t vlen = ((uint16_t)msg[pos + 1] << 8) | msg[pos + 2];
        pos += 3;
        if (pos + vlen > len)
        {
            PROTO_LOG("STREAM dispatch: kv overflow key=0x%02x", key);
            break;
        }
        stream_on_kv(key, msg + pos, vlen);
        pos += vlen;
    }
}

/* ---- RX transport (BLE callback + task) ---------------------------------- */

/* Called in BLE callback context on every write to 0xFFD4. */
static void stream_rx_sink(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    s_conn_handle = conn_handle;

    stream_msg_t msg;
    msg.len    = len;
    msg.p_data = malloc(len);
    if (msg.p_data == NULL)
    {
        PROTO_LOG("STREAM rx: malloc failed len=%d", len);
        return;
    }
    memcpy(msg.p_data, data, len);

    if (os_msg_send(s_stream_queue, &msg, 0) != true)
    {
        PROTO_LOG("STREAM rx: queue full, drop");
        free(msg.p_data);
    }
}

static void stream_cccd_cb(uint16_t conn_handle, bool notify_enabled)
{
    s_conn_handle = conn_handle;
    PROTO_LOG("STREAM cccd: notify=%d", notify_enabled);
}

static void stream_task(void *p_param)
{
    (void)p_param;
    stream_msg_t msg;

    while (true)
    {
        if (os_msg_recv(s_stream_queue, &msg, 0xFFFFFFFF) != true)
        {
            continue;
        }
        stream_dispatch(msg.p_data, msg.len);
        free(msg.p_data);
    }
}

/* ---- Init ---------------------------------------------------------------- */

void hmi_stream_ctrl_init(void)
{
    /* Queue must exist before the GATT service starts delivering RX writes. */
    os_msg_queue_create(&s_stream_queue, "streamQ",
                        STREAM_QUEUE_SIZE, sizeof(stream_msg_t));

    hmi_stream_service_add_service(stream_rx_sink, stream_cccd_cb);

    os_task_create(&s_stream_task, "stream", stream_task,
                   NULL, STREAM_TASK_STACK_SIZE, STREAM_TASK_PRIORITY);
}
