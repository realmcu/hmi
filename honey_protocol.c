#include <string.h>
#include <os_sync.h>
#include "honey_protocol.h"
#include "honey_protocol_internal.h"

/*============================================================================*
 *                              Frame Layout  (Big-Endian)
 *
 *  Offset  Size  Field
 *  0       1     Magic  (0xAB)
 *  1       1     Version/Flags  [7]=reserve  [6]=ack_request  [5]=err_flag  [4]=ack_flag  [3:0]=ver
 *  2       2     Payload length (BE, MSB first)
 *  4       2     CRC16/ARC over payload only (BE, MSB first)
 *  6       2     Sequence ID (BE, MSB first)
 *  8       N     Payload
 *============================================================================*/

#define HONEY_PROTOCOL_MAGIC             0xAB
#define HONEY_PROTOCOL_VERSION           0x00    /* version=0 per spec, occupies bit[3:0] */
#define HONEY_PROTOCOL_HDR_LEN           8
#define HONEY_PROTOCOL_MAX_FRAME_LEN     (HONEY_PROTOCOL_HDR_LEN + HONEY_PROTOCOL_MAX_PAYLOAD_LEN)

#define HONEY_PROTOCOL_FLAG_ACK_REQUEST  (1u << 6)   /* bit[6] per spec */
#define HONEY_PROTOCOL_FLAG_ACK          (1u << 4)   /* bit[4] per spec */
#define HONEY_PROTOCOL_FLAG_ERR          (1u << 5)   /* bit[5] per spec */
#define HONEY_PROTOCOL_FLAG_MASK         (HONEY_PROTOCOL_FLAG_ACK_REQUEST | HONEY_PROTOCOL_FLAG_ERR | HONEY_PROTOCOL_FLAG_ACK)

#define HONEY_PROTOCOL_MAX_RETRY         3
#define HONEY_PROTOCOL_ACK_TIMEOUT_MS    3000

#define HDR_OFF_MAGIC           0
#define HDR_OFF_VER             1
#define HDR_OFF_LEN_HI          2   /* MSB first (Big-Endian) */
#define HDR_OFF_LEN_LO          3
#define HDR_OFF_CRC_HI          4
#define HDR_OFF_CRC_LO          5
#define HDR_OFF_SEQ_HI          6
#define HDR_OFF_SEQ_LO          7

/*============================================================================*
 *                              Static State
 *============================================================================*/

static honey_protocol_send_fn_t s_send    = NULL;
static honey_protocol_receive_fn_t  s_receive     = NULL;
static honey_protocol_frame_cb_t  s_frame_cb = NULL;
static void            *s_ack_sem  = NULL;
static bool     s_ack_ok       = false;
static uint16_t s_seq_id       = 0;
static uint16_t s_wait_ack_seq = 0;
static bool     s_wait_ack_armed = false;

static uint8_t  s_tx_frame[HONEY_PROTOCOL_MAX_FRAME_LEN];
static uint16_t s_tx_frame_len = 0;

static uint8_t  s_rx_buf[HONEY_PROTOCOL_MAX_FRAME_LEN];
static uint16_t s_rx_expected  = 0;
static uint16_t s_rx_offset    = 0;
static bool     s_rx_active    = false;

/*============================================================================*
 *                              Helpers
 *============================================================================*/

/* CRC-16/ARC: poly=0x8005 (reflected 0xA001), init=0x0000,
 * refin=refout=true, xorout=0 - matches the host tool (host computer) parameters. */
static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t len)
{
    while (len--)
    {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0xA001)
                  : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* CRC covers the PAYLOAD ONLY (header bytes excluded) to match the host tool.
 * An empty payload yields 0x0000 (the ARC init value). */
static uint16_t frame_crc(const uint8_t *frame, uint16_t payload_len)
{
    if (payload_len == 0)
    {
        return 0x0000;
    }
    return crc16_update(0x0000, frame + HONEY_PROTOCOL_HDR_LEN, payload_len);
}

static uint16_t build_frame(uint8_t *buf, uint8_t flags,
                            uint16_t seq, const uint8_t *payload, uint16_t len)
{
    buf[HDR_OFF_MAGIC]  = HONEY_PROTOCOL_MAGIC;
    buf[HDR_OFF_VER]    = (HONEY_PROTOCOL_VERSION & 0x0F) | (flags & HONEY_PROTOCOL_FLAG_MASK);
    buf[HDR_OFF_LEN_HI] = (uint8_t)(len >> 8);
    buf[HDR_OFF_LEN_LO] = (uint8_t)(len & 0xFF);
    buf[HDR_OFF_SEQ_HI] = (uint8_t)(seq >> 8);
    buf[HDR_OFF_SEQ_LO] = (uint8_t)(seq & 0xFF);

    if (payload && len > 0)
    {
        memcpy(buf + HONEY_PROTOCOL_HDR_LEN, payload, len);
    }

    uint16_t crc = frame_crc(buf, len);
    buf[HDR_OFF_CRC_HI] = (uint8_t)(crc >> 8);
    buf[HDR_OFF_CRC_LO] = (uint8_t)(crc & 0xFF);

    return HONEY_PROTOCOL_HDR_LEN + len;
}

static void honey_protocol_send_ack(uint16_t seq, bool ok)
{
    uint8_t  ack_buf[HONEY_PROTOCOL_HDR_LEN];
    uint8_t  flags = ok ? HONEY_PROTOCOL_FLAG_ACK : (HONEY_PROTOCOL_FLAG_ACK | HONEY_PROTOCOL_FLAG_ERR);
    uint16_t len   = build_frame(ack_buf, flags, seq, NULL, 0);

    if (s_send)
    {
        s_send(ack_buf, len);
    }
}

void honey_protocol_handle(const uint8_t *data, uint16_t len)
{
    uint16_t pos = 0;

    while (pos < len)
    {
        if (!s_rx_active)
        {
            if (data[pos] != HONEY_PROTOCOL_MAGIC)
            {
                pos++;
                continue;
            }
            s_rx_offset   = 0;
            s_rx_expected = 0;
            s_rx_active   = true;
        }

        uint16_t need = (s_rx_offset < HONEY_PROTOCOL_HDR_LEN)
                        ? (HONEY_PROTOCOL_HDR_LEN - s_rx_offset)
                        : (s_rx_expected - s_rx_offset);

        uint16_t avail = len - pos;
        uint16_t copy  = (avail < need) ? avail : need;

        if (s_rx_offset + copy > HONEY_PROTOCOL_MAX_FRAME_LEN)
        {
            s_rx_active = false;
            pos++;
            continue;
        }

        memcpy(s_rx_buf + s_rx_offset, data + pos, copy);
        s_rx_offset += copy;
        pos         += copy;

        if (s_rx_offset == HONEY_PROTOCOL_HDR_LEN && s_rx_expected == 0)
        {
            if (s_rx_buf[HDR_OFF_MAGIC] != HONEY_PROTOCOL_MAGIC)
            {
                s_rx_active = false;
                continue;
            }
            uint16_t payload_len = ((uint16_t)s_rx_buf[HDR_OFF_LEN_HI] << 8)
                                   | (uint16_t)s_rx_buf[HDR_OFF_LEN_LO];
            if (payload_len > HONEY_PROTOCOL_MAX_PAYLOAD_LEN)
            {
                s_rx_active = false;
                continue;
            }
            s_rx_expected = HONEY_PROTOCOL_HDR_LEN + payload_len;

            if (s_rx_offset < s_rx_expected)
            {
                continue;
            }
        }

        if (s_rx_expected > 0 && s_rx_offset >= s_rx_expected)
        {
            s_rx_active = false;

            uint16_t payload_len = ((uint16_t)s_rx_buf[HDR_OFF_LEN_HI] << 8)
                                   | (uint16_t)s_rx_buf[HDR_OFF_LEN_LO];
            uint16_t rx_crc      = ((uint16_t)s_rx_buf[HDR_OFF_CRC_HI] << 8)
                                   | (uint16_t)s_rx_buf[HDR_OFF_CRC_LO];
            uint16_t seq         = ((uint16_t)s_rx_buf[HDR_OFF_SEQ_HI] << 8)
                                   | (uint16_t)s_rx_buf[HDR_OFF_SEQ_LO];
            uint8_t  flags       = s_rx_buf[HDR_OFF_VER] & HONEY_PROTOCOL_FLAG_MASK;

            bool crc_ok = (frame_crc(s_rx_buf, payload_len) == rx_crc);

            if (flags & HONEY_PROTOCOL_FLAG_ACK)
            {
                if ((flags & HONEY_PROTOCOL_FLAG_ACK_REQUEST) == 0 &&
                    s_wait_ack_armed && seq == s_wait_ack_seq)
                {
                    s_ack_ok = crc_ok && !(flags & HONEY_PROTOCOL_FLAG_ERR);
                    s_wait_ack_armed = false;
                    os_sem_give(s_ack_sem);
                }
            }
            else
            {
                if (!crc_ok)
                {
                    if (flags & HONEY_PROTOCOL_FLAG_ACK_REQUEST)
                    {
                        honey_protocol_send_ack(seq, false);
                    }
                }
                else
                {
                    if (flags & HONEY_PROTOCOL_FLAG_ACK_REQUEST)
                    {
                        honey_protocol_send_ack(seq, true);
                    }
                    if (s_frame_cb && payload_len > 0)
                    {
                        s_frame_cb(s_rx_buf + HONEY_PROTOCOL_HDR_LEN, payload_len);
                    }
                }
            }
        }
    }
}

/*============================================================================*
 *                              Public API
 *============================================================================*/

void honey_protocol_init(honey_protocol_send_fn_t send,
                         honey_protocol_receive_fn_t receive,
                         honey_protocol_frame_cb_t frame_cb)
{
    s_send        = send;
    s_receive     = receive;
    s_frame_cb    = frame_cb;
    s_seq_id       = 0;
    s_tx_frame_len = 0;
    s_rx_active    = false;
    s_rx_offset    = 0;
    s_rx_expected  = 0;
    s_ack_ok       = false;
    s_wait_ack_seq = 0;
    s_wait_ack_armed = false;

    os_sem_create(&s_ack_sem, "honeyack", 0, 1);
}

int honey_protocol_receive(uint8_t *buf, uint16_t max_len)
{
    if (!s_receive)
    {
        return 0;
    }
    return s_receive(buf, max_len);
}

static bool honey_protocol_send_ex(const uint8_t *data, uint16_t len, bool ack_required)
{
    if (!s_send || !data || len == 0 || len > HONEY_PROTOCOL_MAX_PAYLOAD_LEN)
    {
        return false;
    }

    uint16_t seq = s_seq_id++;
    uint8_t flags = ack_required ? HONEY_PROTOCOL_FLAG_ACK_REQUEST : 0;
    s_tx_frame_len = build_frame(s_tx_frame, flags, seq, data, len);

    if (!ack_required)
    {
        s_wait_ack_armed = false;
        return s_send(s_tx_frame, s_tx_frame_len) >= 0;
    }

    /* drain any stale ACK left over from a previous timed-out send */
    while (os_sem_take(s_ack_sem, 0)) { }

    s_wait_ack_seq = seq;
    s_wait_ack_armed = true;

    for (int attempt = 0; attempt < HONEY_PROTOCOL_MAX_RETRY; attempt++)
    {
        s_ack_ok = false;

        if (s_send(s_tx_frame, s_tx_frame_len) < 0)
        {
            s_wait_ack_armed = false;
            return false;
        }

        if (!os_sem_take(s_ack_sem, HONEY_PROTOCOL_ACK_TIMEOUT_MS))
        {
            continue;
        }

        if (s_ack_ok)
        {
            s_wait_ack_armed = false;
            return true;
        }
    }

    s_wait_ack_armed = false;
    return false;
}

bool honey_protocol_send_direct(const uint8_t *data, uint16_t len, bool ack_required)
{
    return honey_protocol_send_ex(data, len, ack_required);
}
