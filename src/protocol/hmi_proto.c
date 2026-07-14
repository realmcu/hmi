/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include <os_sync.h>
#include "hmi_proto.h"

/*============================================================================*
 *                              Frame Layout  (Big-Endian)
 *
 *  Offset  Size  Field
 *  0       1     Magic  (0xAB)
 *  1       1     Version/Flags  [7:6]=reserve  [5]=err_flag  [4]=ack_flag  [3:0]=ver
 *  2       2     Payload length (BE, MSB first)
 *  4       2     CRC16 over [0..3] + payload (BE, MSB first)
 *  6       2     Sequence ID (BE, MSB first)
 *  8       N     Payload
 *============================================================================*/

#define PROTO_MAGIC             0xAB
#define PROTO_VERSION           0x00    /* version=0 per spec, occupies bit[3:0] */
#define PROTO_HDR_LEN           8
#define PROTO_MAX_FRAME_LEN     (PROTO_HDR_LEN + PROTO_MAX_PAYLOAD_LEN)

#define PROTO_FLAG_ACK          (1u << 4)   /* bit[4] per spec */
#define PROTO_FLAG_ERR          (1u << 5)   /* bit[5] per spec */

#define PROTO_MAX_RETRY         3
#define PROTO_ACK_TIMEOUT_MS    3000

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

static proto_send_fn_t s_send    = NULL;
static proto_receive_fn_t  s_receive     = NULL;
static proto_recv_cb_t  s_recv_cb  = NULL;
static void            *s_ack_sem  = NULL;
static bool     s_ack_ok       = false;
static uint16_t s_seq_id       = 0;

static uint8_t  s_tx_frame[PROTO_MAX_FRAME_LEN];
static uint16_t s_tx_frame_len = 0;

static uint8_t  s_rx_buf[PROTO_MAX_FRAME_LEN];
static uint16_t s_rx_expected  = 0;
static uint16_t s_rx_offset    = 0;
static bool     s_rx_active    = false;

/*============================================================================*
 *                              Helpers
 *============================================================================*/

/* CRC-16/KERMIT: poly=0x1021 reflected, init=0x0000 -- same as btxfcs */
static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t len)
{
    while (len--)
    {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
        }
    }
    return crc;
}

static uint16_t frame_crc(const uint8_t *frame, uint16_t payload_len)
{
    uint16_t crc = crc16_update(0x0000, frame, 4);
    if (payload_len > 0)
    {
        crc = crc16_update(crc, frame + PROTO_HDR_LEN, payload_len);
    }
    return crc;
}

static uint16_t build_frame(uint8_t *buf, uint8_t flags,
                            uint16_t seq, const uint8_t *payload, uint16_t len)
{
    buf[HDR_OFF_MAGIC]  = PROTO_MAGIC;
    buf[HDR_OFF_VER]    = (PROTO_VERSION & 0x0F) | (flags & 0x30);
    buf[HDR_OFF_LEN_LO] = (uint8_t)(len & 0xFF);
    buf[HDR_OFF_LEN_HI] = (uint8_t)(len >> 8);
    buf[HDR_OFF_SEQ_LO] = (uint8_t)(seq & 0xFF);
    buf[HDR_OFF_SEQ_HI] = (uint8_t)(seq >> 8);

    if (payload && len > 0)
    {
        memcpy(buf + PROTO_HDR_LEN, payload, len);
    }

    uint16_t crc = frame_crc(buf, len);
    buf[HDR_OFF_CRC_LO] = (uint8_t)(crc & 0xFF);
    buf[HDR_OFF_CRC_HI] = (uint8_t)(crc >> 8);

    return PROTO_HDR_LEN + len;
}

static void proto_send_ack(uint16_t seq, bool ok)
{
    uint8_t  ack_buf[PROTO_HDR_LEN];
    uint8_t  flags = ok ? PROTO_FLAG_ACK : (PROTO_FLAG_ACK | PROTO_FLAG_ERR);
    uint16_t len   = build_frame(ack_buf, flags, seq, NULL, 0);

    if (s_send)
    {
        s_send(ack_buf, len);
    }
}

void proto_handle(const uint8_t *data, uint16_t len)
{
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
            s_rx_offset   = 0;
            s_rx_expected = 0;
            s_rx_active   = true;
        }

        uint16_t need = (s_rx_offset < PROTO_HDR_LEN)
                        ? (PROTO_HDR_LEN - s_rx_offset)
                        : (s_rx_expected - s_rx_offset);

        uint16_t avail = len - pos;
        uint16_t copy  = (avail < need) ? avail : need;

        if (s_rx_offset + copy > PROTO_MAX_FRAME_LEN)
        {
            s_rx_active = false;
            pos++;
            continue;
        }

        memcpy(s_rx_buf + s_rx_offset, data + pos, copy);
        s_rx_offset += copy;
        pos         += copy;

        if (s_rx_offset == PROTO_HDR_LEN && s_rx_expected == 0)
        {
            if (s_rx_buf[HDR_OFF_MAGIC] != PROTO_MAGIC)
            {
                s_rx_active = false;
                continue;
            }
            uint16_t payload_len = (uint16_t)s_rx_buf[HDR_OFF_LEN_LO]
                                   | ((uint16_t)s_rx_buf[HDR_OFF_LEN_HI] << 8);
            if (payload_len > PROTO_MAX_PAYLOAD_LEN)
            {
                s_rx_active = false;
                continue;
            }
            s_rx_expected = PROTO_HDR_LEN + payload_len;

            if (s_rx_offset < s_rx_expected)
            {
                continue;
            }
        }

        if (s_rx_expected > 0 && s_rx_offset >= s_rx_expected)
        {
            s_rx_active = false;

            uint16_t payload_len = (uint16_t)s_rx_buf[HDR_OFF_LEN_LO]
                                   | ((uint16_t)s_rx_buf[HDR_OFF_LEN_HI] << 8);
            uint16_t rx_crc      = (uint16_t)s_rx_buf[HDR_OFF_CRC_LO]
                                   | ((uint16_t)s_rx_buf[HDR_OFF_CRC_HI] << 8);
            uint16_t seq         = (uint16_t)s_rx_buf[HDR_OFF_SEQ_LO]
                                   | ((uint16_t)s_rx_buf[HDR_OFF_SEQ_HI] << 8);
            uint8_t  flags       = s_rx_buf[HDR_OFF_VER] & 0x30;

            bool crc_ok = (frame_crc(s_rx_buf, payload_len) == rx_crc);

            if (flags & PROTO_FLAG_ACK)
            {
                s_ack_ok = crc_ok && !(flags & PROTO_FLAG_ERR);
                os_sem_give(s_ack_sem);
            }
            else
            {
                if (!crc_ok)
                {
                    proto_send_ack(seq, false);
                }
                else
                {
                    proto_send_ack(seq, true);
                    if (s_recv_cb && payload_len > 0)
                    {
                        s_recv_cb(s_rx_buf + PROTO_HDR_LEN, payload_len);
                    }
                }
            }
        }
    }
}

/*============================================================================*
 *                              Public API
 *============================================================================*/

void proto_init(proto_send_fn_t send, proto_receive_fn_t receive, proto_recv_cb_t recv_cb)
{
    s_send        = send;
    s_receive     = receive;
    s_recv_cb      = recv_cb;
    s_seq_id       = 0;
    s_tx_frame_len = 0;
    s_rx_active    = false;
    s_rx_offset    = 0;
    s_rx_expected  = 0;
    s_ack_ok       = false;

    os_sem_create(&s_ack_sem, "protoack", 0, 1);
}

int proto_receive(uint8_t *buf, uint16_t max_len)
{
    if (!s_receive)
    {
        return 0;
    }
    return s_receive(buf, max_len);
}

bool proto_send(const uint8_t *data, uint16_t len)
{
    if (!s_send || !data || len == 0 || len > PROTO_MAX_PAYLOAD_LEN)
    {
        return false;
    }

    uint16_t seq = s_seq_id++;
    s_tx_frame_len = build_frame(s_tx_frame, 0, seq, data, len);

    /* drain any stale ACK left over from a previous timed-out send */
    while (os_sem_take(s_ack_sem, 0)) { }

    for (int attempt = 0; attempt < PROTO_MAX_RETRY; attempt++)
    {
        s_ack_ok = false;

        if (s_send(s_tx_frame, s_tx_frame_len) < 0)
        {
            return false;
        }

        if (!os_sem_take(s_ack_sem, PROTO_ACK_TIMEOUT_MS))
        {
            continue;
        }

        if (s_ack_ok)
        {
            return true;
        }
    }

    return false;
}
