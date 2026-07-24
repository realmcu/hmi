/**
 * @file  hmi_l2_xfer_client.c
 * @brief Device-to-device file SEND (central side).  See header for the stack.
 *
 * Self-contained proto(L1) codec + xfer state machine.  Every outgoing proto
 * frame is kept <= one BLE write (chunk sized to MTU-18) so no fragmentation is
 * needed; the peer's proto_handle reassembles from its RX byte stream anyway.
 */
#include "hmi_l2_xfer_client.h"
#include "hmi_l2.h"
#include "hmi_protocal_task.h"   /* hmi_proto_post_call -> run on l2_task */

#include <string.h>
#include <trace.h>
#include <os_timer.h>
#include <gap.h>
#include <gap_le.h>
#include <gap_conn_le.h>

/*============================================================================*
 *   proto(L1) framing -- MUST match component/protocol/hmi_proto.c
 *============================================================================*/
#define PROTO_MAGIC        0xABu
#define PROTO_HDR_LEN      8u
#define PROTO_VERSION      0x00u
#define PROTO_FLAG_ACK     (1u << 4)
#define PROTO_FLAG_ERR     (1u << 5)

#define HDR_MAGIC   0
#define HDR_VER     1
#define HDR_LEN_HI  2
#define HDR_LEN_LO  3
#define HDR_CRC_HI  4
#define HDR_CRC_LO  5
#define HDR_SEQ_HI  6
#define HDR_SEQ_LO  7

/*============================================================================*
 *   Sizing
 *============================================================================*/
#define XC_MAX_DATA        229u   /* data bytes/frame fitting one 247-MTU write */
#define XC_TX_FRAME_MAX    (PROTO_HDR_LEN + 5u + 2u + XC_MAX_DATA)  /* 244 */
#define XC_RX_MAX          (PROTO_HDR_LEN + 64u)                    /* responses are tiny */
/* Wait timeouts, aligned to the Android reference (l2_file_transfer.dart):
 * the handshake gets the most headroom because the peer erases flash
 * (fdb_bf_create) before replying BEGIN_RSP. */
#define XC_BEGIN_MS        30000u /* BEGIN_RSP wait (peer flash erase; Android=30s) */
#define XC_DATA_MS         25000u /* per-DATA watchdog        (Android dataAck=25s) */
#define XC_END_MS          25000u /* END_RSP wait (peer commit; Android verify=25s) */
#define XC_TIMER_ID        0x2Cu
#define GAP_SUCCESS_CAUSE  0x0000u

/*============================================================================*
 *   State
 *============================================================================*/
typedef enum
{
    XC_IDLE = 0,
    XC_BEGIN_WAIT,   /* BEGIN_REQ sent, awaiting BEGIN_RSP */
    XC_SENDING,      /* pumping DATA chunks (stop-and-wait) */
    XC_END_WAIT,     /* END_REQ sent, awaiting END_RSP */
} T_XC_STATE;

static T_XC_STATE   s_state = XC_IDLE;

static uint8_t      s_conn_id;
static T_CLIENT_ID  s_client_id;
static uint16_t     s_cmd_handle;

static const uint8_t *s_src;
static uint32_t     s_total;
static uint32_t     s_sent;      /* bytes confirmed-sent (ATT write-done) */
static uint16_t     s_chunk;     /* data bytes per DATA frame */
static uint16_t     s_seq;       /* xfer DATA sequence (0-based) */
static uint8_t      s_type;

static uint16_t     s_tx_seq;    /* proto tx sequence */
static xfer_client_done_cb_t s_done_cb;

static void        *s_timer = NULL;

/* proto rx reassembly */
static uint8_t      s_rx[XC_RX_MAX];
static uint16_t     s_rx_off;
static uint16_t     s_rx_expected;
static bool         s_rx_active;

/*============================================================================*
 *   Forward decls
 *============================================================================*/
static void xc_send_next_data(void);
static void xc_finish(T_XFER_CLIENT_RESULT result);
static void xc_fail(T_XFER_CLIENT_RESULT result);
static void xc_timer_cb(void *p_handle);
static bool xc_send_cmd(uint8_t key, const uint8_t *val, uint16_t vlen);
static void xc_pump_cb(l2_msg_t *p_msg);

/* Schedule the next DATA/END send on l2_task.  client_attr_write MUST NOT be
 * issued from a GATT stack callback (write-completion or notify) -- doing so is
 * rejected with cause 0x3 while the previous request is still settling -- so we
 * always pump from l2_task instead. */
static void xc_pump(void)
{
    if (!hmi_proto_post_call(xc_pump_cb, NULL))
    {
        APP_PRINT_ERROR0("[xferC] pump post failed (l2 queue full)");
        xc_fail(XFER_CLIENT_ERR_STATE);
    }
}

/*============================================================================*
 *   Helpers
 *============================================================================*/
/* CRC-16/ARC, identical parameters to hmi_proto.c. */
static uint16_t xc_crc16(uint16_t crc, const uint8_t *data, uint16_t len)
{
    while (len--)
    {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc & 0x0001u) ? (uint16_t)((crc >> 1) ^ 0xA001u)
                  : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* Largest DATA payload that still fits one BLE write for the current MTU. */
static uint16_t xc_calc_chunk(void)
{
    uint16_t mtu = 23;
    le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu, s_conn_id);
    int budget = (int)mtu - 3 - (int)PROTO_HDR_LEN - 2 - 3 - 2; /* -att -proto -l2hdr -kvpfx -seq */
    if (budget < 1) { budget = 1; }
    if (budget > XC_MAX_DATA) { budget = XC_MAX_DATA; }
    return (uint16_t)budget;
}

static void xc_timer_start(uint32_t ms)
{
    if (s_timer == NULL)
    {
        os_timer_create(&s_timer, "xferC", XC_TIMER_ID, ms, false, xc_timer_cb);
    }
    os_timer_restart(&s_timer, ms);
}

static void xc_timer_stop(void)
{
    if (s_timer != NULL)
    {
        os_timer_stop(&s_timer);
    }
}

/* Build a proto-framed L2 xfer command and write it to the peer CMD char.
 * write_type: GATT_WRITE_TYPE_REQ for paced/confirmed, _CMD for fire-and-forget. */
static bool xc_write_frame(uint8_t flags, uint8_t key, const uint8_t *val, uint16_t vlen,
                           T_GATT_WRITE_TYPE write_type)
{
    uint8_t  frame[XC_TX_FRAME_MAX];
    uint16_t payload_len = 0;

    if (!(flags & PROTO_FLAG_ACK))
    {
        /* L2 frame: [cmd][ver][key][len_hi][len_lo][value] */
        uint16_t p = PROTO_HDR_LEN;
        frame[p++] = HMI_L2_CMD_FILE_XFER;
        frame[p++] = 0x00u;                       /* L2 version/reserve */
        frame[p++] = key;
        frame[p++] = (uint8_t)((vlen >> 8) & 0xFFu);   /* full high byte (parser reads 16-bit) */
        frame[p++] = (uint8_t)(vlen & 0xFFu);
        if (val && vlen)
        {
            memcpy(&frame[p], val, vlen);
            p += vlen;
        }
        payload_len = p - PROTO_HDR_LEN;
    }

    frame[HDR_MAGIC]  = PROTO_MAGIC;
    frame[HDR_VER]    = (PROTO_VERSION & 0x0Fu) | (flags & 0x30u);
    frame[HDR_LEN_HI] = (uint8_t)(payload_len >> 8);
    frame[HDR_LEN_LO] = (uint8_t)(payload_len & 0xFFu);
    frame[HDR_SEQ_HI] = (uint8_t)(s_tx_seq >> 8);
    frame[HDR_SEQ_LO] = (uint8_t)(s_tx_seq & 0xFFu);
    uint16_t crc = (payload_len == 0) ? 0 : xc_crc16(0, &frame[PROTO_HDR_LEN], payload_len);
    frame[HDR_CRC_HI] = (uint8_t)(crc >> 8);
    frame[HDR_CRC_LO] = (uint8_t)(crc & 0xFFu);
    s_tx_seq++;

    T_GAP_CAUSE c = client_attr_write(s_conn_id, s_client_id, write_type,
                                      s_cmd_handle, (uint16_t)(PROTO_HDR_LEN + payload_len), frame);
    if (c != GAP_CAUSE_SUCCESS)
    {
        APP_PRINT_ERROR1("[xferC] client_attr_write failed 0x%x", c);
        return false;
    }
    return true;
}

/* Send an L2 xfer command (paced, write-with-response). */
static bool xc_send_cmd(uint8_t key, const uint8_t *val, uint16_t vlen)
{
    return xc_write_frame(0, key, val, vlen, GATT_WRITE_TYPE_REQ);
}

/* ACK a peer data frame (fire-and-forget); echoes the peer's seq. */
static void xc_send_ack(uint16_t peer_seq)
{
    uint16_t saved = s_tx_seq;
    s_tx_seq = peer_seq;                 /* echo peer seq in the ACK */
    (void)xc_write_frame(PROTO_FLAG_ACK, 0, NULL, 0, GATT_WRITE_TYPE_CMD);
    s_tx_seq = saved;                    /* keep our own tx seq monotonic */
}

/*============================================================================*
 *   xfer response handling
 *============================================================================*/
static void xc_on_xfer_rsp(uint8_t key, const uint8_t *val, uint16_t vlen)
{
    switch (key)
    {
    case HMI_L2_XFER_BEGIN_RSP:
        if (s_state != XC_BEGIN_WAIT || vlen < 1)
        {
            return;
        }
        if (val[0] == HMI_L2_XFER_BEGIN_OK)
        {
            xc_timer_stop();
            s_state = XC_SENDING;
            s_seq   = 0;
            s_sent  = 0;
            APP_PRINT_INFO2("[xferC] BEGIN_RSP OK, sending %u bytes, chunk %d",
                            (unsigned)s_total, s_chunk);
            xc_pump();   /* send DATA#0 from l2_task, not this notify callback */
        }
        else
        {
            APP_PRINT_ERROR1("[xferC] BEGIN rejected, status %d", val[0]);
            xc_finish(XFER_CLIENT_ERR_BEGIN);
        }
        break;

    case HMI_L2_XFER_END_RSP:
        if (s_state != XC_END_WAIT)
        {
            return;
        }
        xc_timer_stop();
        if (vlen >= 1 && val[0] == HMI_L2_XFER_END_OK)
        {
            APP_PRINT_INFO1("[xferC] END_RSP OK -- %u bytes sent", (unsigned)s_sent);
            xc_finish(XFER_CLIENT_OK);
        }
        else
        {
            APP_PRINT_ERROR1("[xferC] END_RSP status %d", (vlen >= 1) ? val[0] : 0xFF);
            xc_fail(XFER_CLIENT_ERR_STATE);
        }
        break;

    case HMI_L2_XFER_ABORT:
        APP_PRINT_ERROR0("[xferC] peer sent ABORT");
        xc_finish(XFER_CLIENT_ERR_ABORT);
        break;

    default:
        break;
    }
}

/* Validate one complete proto frame, ACK data frames, dispatch L2 payload. */
static void xc_handle_frame(const uint8_t *frame)
{
    uint16_t payload_len = ((uint16_t)frame[HDR_LEN_HI] << 8) | frame[HDR_LEN_LO];
    uint16_t rx_crc      = ((uint16_t)frame[HDR_CRC_HI] << 8) | frame[HDR_CRC_LO];
    uint16_t seq         = ((uint16_t)frame[HDR_SEQ_HI] << 8) | frame[HDR_SEQ_LO];
    uint8_t  flags       = frame[HDR_VER] & 0x30u;

    uint16_t calc = (payload_len == 0) ? 0 : xc_crc16(0, &frame[PROTO_HDR_LEN], payload_len);
    if (calc != rx_crc)
    {
        APP_PRINT_ERROR2("[xferC] rx crc bad: calc 0x%x rx 0x%x", calc, rx_crc);
        return;
    }

    if (flags & PROTO_FLAG_ACK)
    {
        /* Peer ACK of one of our frames.  NOT used for pacing: the peer's
         * app-level proto ACK can arrive BEFORE our own ATT write-response for
         * the same DATA frame, so it does not mean the WRITE_REQ is done.  We
         * pace strictly on the ATT write-response (on_write_done). */
        return;
    }

    /* Data frame from the peer -> must ACK (its proto_send blocks on our ACK). */
    xc_send_ack(seq);

    /* L2: [cmd][ver][key][len_hi][len_lo][value] */
    if (payload_len < 5)
    {
        return;
    }
    const uint8_t *l2 = &frame[PROTO_HDR_LEN];
    if (l2[0] != HMI_L2_CMD_FILE_XFER)
    {
        return;
    }
    uint8_t  key  = l2[2];
    uint16_t vlen = ((uint16_t)l2[3] << 8) | l2[4];
    if ((uint32_t)5 + vlen > payload_len)
    {
        return;
    }
    xc_on_xfer_rsp(key, &l2[5], vlen);
}

/*============================================================================*
 *   DATA pump
 *============================================================================*/
static void xc_send_next_data(void)
{
    if (s_sent >= s_total)
    {
        /* All data delivered -> END_REQ.  crc32 = 0 (whole-file CRC disabled). */
        uint8_t crc32[4] = { 0, 0, 0, 0 };
        s_state = XC_END_WAIT;
        xc_timer_start(XC_END_MS);
        if (!xc_send_cmd(HMI_L2_XFER_END_REQ, crc32, sizeof(crc32)))
        {
            xc_finish(XFER_CLIENT_ERR_LINK);
        }
        return;
    }

    uint16_t n = s_chunk;
    if ((uint32_t)n > s_total - s_sent)
    {
        n = (uint16_t)(s_total - s_sent);
    }

    uint8_t buf[2 + XC_MAX_DATA];
    buf[0] = (uint8_t)(s_seq >> 8);
    buf[1] = (uint8_t)(s_seq & 0xFFu);
    memcpy(&buf[2], s_src + s_sent, n);

    /* Per-DATA watchdog: fires if the ATT write-response never arrives. */
    xc_timer_start(XC_DATA_MS);
    if (!xc_send_cmd(HMI_L2_XFER_DATA, buf, (uint16_t)(2 + n)))
    {
        xc_fail(XFER_CLIENT_ERR_LINK);
    }
    /* advance on the ATT write-response (on_write_done -> xc_pump) */
}

static void xc_pump_cb(l2_msg_t *p_msg)
{
    (void)p_msg;
    xc_send_next_data();
}

static void xc_finish(T_XFER_CLIENT_RESULT result)
{
    xc_timer_stop();
    s_state = XC_IDLE;

    xfer_client_done_cb_t cb   = s_done_cb;
    uint32_t              sent = s_sent;
    s_done_cb = NULL;

    APP_PRINT_INFO2("[xferC] finished result %d, %u bytes", result, (unsigned)sent);
    if (cb != NULL)
    {
        cb(result, sent);
    }
}

/* Error finish: best-effort ABORT so the peer frees its xfer session (otherwise
 * the next transfer gets BEGIN_BUSY), then finish.  Harmless if the link is
 * already down (the write just fails).  Not used for OK / peer-ABORT / link-loss. */
static void xc_fail(T_XFER_CLIENT_RESULT result)
{
    uint8_t reason = HMI_L2_XFER_ABORT_ERROR;
    (void)xc_send_cmd(HMI_L2_XFER_ABORT, &reason, 1);
    xc_finish(result);
}

/* os_timer callback (timer task context): handshake / end-wait timed out. */
static void xc_timer_cb(void *p_handle)
{
    (void)p_handle;
    if (s_state == XC_IDLE)
    {
        return;
    }
    APP_PRINT_ERROR1("[xferC] timeout in state %d", s_state);
    if (s_state == XC_BEGIN_WAIT)
    {
        /* Peer may not have created a session yet -> don't send ABORT (its
         * ABORT handler would fdb_bf_abort a NULL/stale file). */
        xc_finish(XFER_CLIENT_ERR_TIMEOUT);
    }
    else
    {
        /* SENDING / END_WAIT: peer has an active session -> ABORT frees it. */
        xc_fail(XFER_CLIENT_ERR_TIMEOUT);
    }
}

/*============================================================================*
 *   Public API
 *============================================================================*/
bool hmi_l2_xfer_client_start(uint8_t conn_id, T_CLIENT_ID client_id, uint16_t cmd_handle,
                              uint8_t type, const uint8_t *src, uint32_t total,
                              const char *fname, xfer_client_done_cb_t done_cb)
{
    if (s_state != XC_IDLE)
    {
        APP_PRINT_WARN1("[xferC] start rejected, busy (state %d)", s_state);
        return false;
    }
    if (src == NULL || total == 0 || cmd_handle == 0)
    {
        APP_PRINT_ERROR0("[xferC] start bad args");
        return false;
    }

    s_conn_id    = conn_id;
    s_client_id  = client_id;
    s_cmd_handle = cmd_handle;
    s_type       = type;
    s_src        = src;
    s_total      = total;
    s_sent        = 0;
    s_seq         = 0;
    s_tx_seq      = 0;
    s_done_cb     = done_cb;
    s_rx_active   = false;
    s_rx_off     = 0;
    s_rx_expected = 0;
    s_chunk      = xc_calc_chunk();

    /* BEGIN_REQ value: [type][total(4 BE)][chunk(2 BE)][fname_len][fname...] */
    uint8_t  v[8 + 32];
    uint16_t p = 0;
    v[p++] = s_type;
    v[p++] = (uint8_t)(s_total >> 24);
    v[p++] = (uint8_t)(s_total >> 16);
    v[p++] = (uint8_t)(s_total >> 8);
    v[p++] = (uint8_t)(s_total & 0xFFu);
    v[p++] = (uint8_t)(s_chunk >> 8);
    v[p++] = (uint8_t)(s_chunk & 0xFFu);
    uint8_t fn_len = 0;
    if (fname != NULL)
    {
        size_t l = strlen(fname);
        fn_len = (l > 32) ? 32 : (uint8_t)l;
    }
    v[p++] = fn_len;
    if (fn_len)
    {
        memcpy(&v[p], fname, fn_len);
        p += fn_len;
    }

    s_state = XC_BEGIN_WAIT;
    xc_timer_start(XC_BEGIN_MS);   /* 30s: peer runs fdb_bf_create (flash erase) */

    if (!xc_send_cmd(HMI_L2_XFER_BEGIN_REQ, v, p))
    {
        xc_finish(XFER_CLIENT_ERR_LINK);
        return false;
    }
    APP_PRINT_INFO2("[xferC] BEGIN_REQ sent, total %u, chunk %d", (unsigned)s_total, s_chunk);
    return true;
}

void hmi_l2_xfer_client_abort(void)
{
    if (s_state == XC_IDLE)
    {
        return;
    }
    uint8_t reason = HMI_L2_XFER_ABORT_USER;
    (void)xc_send_cmd(HMI_L2_XFER_ABORT, &reason, 1);
    xc_finish(XFER_CLIENT_ERR_ABORT);
}

bool hmi_l2_xfer_client_busy(void)
{
    return s_state != XC_IDLE;
}

void hmi_l2_xfer_client_on_notify(const uint8_t *data, uint16_t len)
{
    if (s_state == XC_IDLE || data == NULL)
    {
        return;
    }

    uint16_t pos = 0;
    while (pos < len)
    {
        if (!s_rx_active)
        {
            if (data[pos] != PROTO_MAGIC)
            {
                pos++;
                continue;
            }
            s_rx_off      = 0;
            s_rx_expected = 0;
            s_rx_active   = true;
        }

        uint16_t need = (s_rx_off < PROTO_HDR_LEN) ? (uint16_t)(PROTO_HDR_LEN - s_rx_off)
                        : (uint16_t)(s_rx_expected - s_rx_off);
        uint16_t avail = (uint16_t)(len - pos);
        uint16_t copy  = (avail < need) ? avail : need;

        if (s_rx_off + copy > XC_RX_MAX)
        {
            /* frame larger than our tiny response buffer -> resync */
            s_rx_active = false;
            pos++;
            continue;
        }

        memcpy(s_rx + s_rx_off, data + pos, copy);
        s_rx_off = (uint16_t)(s_rx_off + copy);
        pos      = (uint16_t)(pos + copy);

        if (s_rx_off == PROTO_HDR_LEN && s_rx_expected == 0)
        {
            if (s_rx[HDR_MAGIC] != PROTO_MAGIC)
            {
                s_rx_active = false;
                continue;
            }
            uint16_t pl = ((uint16_t)s_rx[HDR_LEN_HI] << 8) | s_rx[HDR_LEN_LO];
            if (PROTO_HDR_LEN + pl > XC_RX_MAX)
            {
                s_rx_active = false;
                continue;
            }
            s_rx_expected = (uint16_t)(PROTO_HDR_LEN + pl);
            if (s_rx_off < s_rx_expected)
            {
                continue;
            }
        }

        if (s_rx_expected > 0 && s_rx_off >= s_rx_expected)
        {
            s_rx_active = false;
            xc_handle_frame(s_rx);
        }
    }
}

void hmi_l2_xfer_client_on_write_done(uint8_t write_type, uint16_t handle, uint16_t cause)
{
    /* Pace strictly on the DATA WRITE_REQ completion.  Ignore ACK writes
     * (WRITE_CMD) and anything outside the DATA phase -- otherwise an ACK's
     * completion (which fires after we've entered SENDING) would be mistaken
     * for a DATA completion and wrongly advance the byte counter. */
    if (s_state != XC_SENDING || handle != s_cmd_handle ||
        write_type != GATT_WRITE_TYPE_REQ)
    {
        return;
    }

    if (cause != GAP_SUCCESS_CAUSE)
    {
        APP_PRINT_ERROR1("[xferC] DATA write failed cause 0x%x", cause);
        xc_fail(XFER_CLIENT_ERR_LINK);
        return;
    }

    /* DATA WRITE_REQ complete -> advance and pump the next chunk from l2_task.
     * Issuing client_attr_write from THIS completion callback is rejected with
     * cause 0x3, so we defer via hmi_proto_post_call. */
    uint16_t n = s_chunk;
    if ((uint32_t)n > s_total - s_sent)
    {
        n = (uint16_t)(s_total - s_sent);
    }
    s_sent += n;
    s_seq++;
    xc_pump();
}

void hmi_l2_xfer_client_on_disconnect(void)
{
    if (s_state != XC_IDLE)
    {
        APP_PRINT_ERROR0("[xferC] link lost mid-transfer");
        xc_finish(XFER_CLIENT_ERR_LINK);
    }
}
