/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>

#include "hmi_proto.h"
#include "proto_log.h"

/*============================================================================*
 *                          L1 frame layout (big-endian)
 *
 *  0      Magic 0xAB
 *  1      control: [7] rsv [6] ack req [5] err [4] ack [3:0] version
 *  2..3   payload length
 *  4..5   CRC-16/ARC over the payload only (spec 1.3)
 *  6..7   sequence id
 *  8..N   payload (<= 248 bytes)
 *============================================================================*/

#define HDR_OFF_MAGIC    0u
#define HDR_OFF_CTRL     1u
#define HDR_OFF_LEN_HI   2u
#define HDR_OFF_LEN_LO   3u
#define HDR_OFF_CRC_HI   4u
#define HDR_OFF_CRC_LO   5u
#define HDR_OFF_SEQ_HI   6u
#define HDR_OFF_SEQ_LO   7u

static proto_tx_fn_t   s_tx;
static proto_l2_sink_t s_sink;

static uint16_t s_l1_tx_seq;

/* Reassembly state. One frame in flight; the sink consumes it synchronously. */
static uint8_t  s_rx_buf[PROTO_L1_MAX_FRAME];
static uint16_t s_rx_len;
static uint16_t s_rx_expected;

uint16_t proto_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000;
    uint16_t i;

    if (data == NULL)
    {
        return 0;
    }
    while (len--)
    {
        crc ^= *data++;
        for (i = 0; i < 8u; i++)
        {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

bool proto_crc_self_test(void)
{
    static const uint8_t vector[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    return proto_crc16(vector, sizeof(vector)) == 0xBB3D;
}

static void proto_send_control(uint8_t control, uint16_t seq)
{
    uint8_t frame[PROTO_L1_HDR_LEN];

    if (s_tx == NULL)
    {
        return;
    }

    frame[HDR_OFF_MAGIC]  = PROTO_L1_MAGIC;
    frame[HDR_OFF_CTRL]   = (uint8_t)(control | PROTO_L1_VERSION);
    frame[HDR_OFF_LEN_HI] = 0;
    frame[HDR_OFF_LEN_LO] = 0;
    frame[HDR_OFF_CRC_HI] = 0;          /* empty payload: CRC stays at init 0 */
    frame[HDR_OFF_CRC_LO] = 0;
    frame[HDR_OFF_SEQ_HI] = (uint8_t)(seq >> 8);
    frame[HDR_OFF_SEQ_LO] = (uint8_t)(seq & 0xFFu);

    s_tx(frame, sizeof(frame));
}

static void proto_frame_complete(void)
{
    uint16_t payload_len = (uint16_t)(s_rx_expected - PROTO_L1_HDR_LEN);
    uint16_t received_crc = (uint16_t)(((uint16_t)s_rx_buf[HDR_OFF_CRC_HI] << 8) |
                                       s_rx_buf[HDR_OFF_CRC_LO]);
    uint16_t seq = (uint16_t)(((uint16_t)s_rx_buf[HDR_OFF_SEQ_HI] << 8) |
                              s_rx_buf[HDR_OFF_SEQ_LO]);
    uint8_t control = s_rx_buf[HDR_OFF_CTRL];
    const uint8_t *payload = s_rx_buf + PROTO_L1_HDR_LEN;
    bool ack_requested = (control & PROTO_CTRL_ACK_REQ) != 0;

    s_rx_len = 0;
    s_rx_expected = 0;

    if (control & PROTO_CTRL_BIT_ACK)
    {
        /* Streaming peer: this endpoint never waits for ACKs. */
        return;
    }

    if (received_crc != proto_crc16(payload, payload_len))
    {
        PROTO_LOG("L1 CRC error seq=%u len=%u", seq, payload_len);
        if (ack_requested)
        {
            proto_send_control(PROTO_CTRL_ERROR, seq);
        }
        return;
    }

    if (ack_requested)
    {
        proto_send_control(PROTO_CTRL_ACK, seq);
    }

    if (!(control & PROTO_CTRL_BIT_ERR) && s_sink != NULL && payload_len > 0)
    {
        s_sink(payload, payload_len);
    }
}

void proto_init(proto_tx_fn_t tx, proto_l2_sink_t sink)
{
    s_tx = tx;
    s_sink = sink;
    s_l1_tx_seq = 0;
    s_rx_len = 0;
    s_rx_expected = 0;
}

void proto_rx_feed(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if (data == NULL || len == 0)
    {
        return;
    }

    for (i = 0; i < len; i++)
    {
        uint8_t byte = data[i];

        if (s_rx_len == 0 && byte != PROTO_L1_MAGIC)
        {
            continue;
        }
        if (s_rx_len >= sizeof(s_rx_buf))
        {
            s_rx_len = 0;
            s_rx_expected = 0;
            continue;
        }

        s_rx_buf[s_rx_len++] = byte;

        if (s_rx_len == PROTO_L1_HDR_LEN)
        {
            uint16_t payload_len = (uint16_t)(((uint16_t)s_rx_buf[HDR_OFF_LEN_HI] << 8) |
                                              s_rx_buf[HDR_OFF_LEN_LO]);
            if (payload_len > PROTO_L1_MAX_PAYLOAD)
            {
                PROTO_LOG("L1 payload too long: %u", payload_len);
                s_rx_len = 0;
                s_rx_expected = 0;
                continue;
            }
            s_rx_expected = (uint16_t)(PROTO_L1_HDR_LEN + payload_len);
        }

        if (s_rx_expected != 0 && s_rx_len == s_rx_expected)
        {
            proto_frame_complete();
        }
    }
}

uint16_t proto_l2_build(uint8_t cmd, uint8_t version, uint8_t key,
                        const uint8_t *value, uint16_t len,
                        uint8_t *out, uint16_t out_cap)
{
    uint16_t crc;
    uint16_t total;

    if (out == NULL || len > PROTO_L2_MAX_VALUE || (len > 0 && value == NULL))
    {
        return 0;
    }

    total = (uint16_t)(PROTO_L1_HDR_LEN + PROTO_L2_HDR_LEN + len);
    if (total > out_cap)
    {
        return 0;
    }

    out[HDR_OFF_MAGIC]  = PROTO_L1_MAGIC;
    out[HDR_OFF_CTRL]   = (uint8_t)(PROTO_CTRL_DATA | PROTO_L1_VERSION);
    out[HDR_OFF_LEN_HI] = (uint8_t)((PROTO_L2_HDR_LEN + len) >> 8);
    out[HDR_OFF_LEN_LO] = (uint8_t)((PROTO_L2_HDR_LEN + len) & 0xFFu);
    out[HDR_OFF_SEQ_HI] = (uint8_t)(s_l1_tx_seq >> 8);
    out[HDR_OFF_SEQ_LO] = (uint8_t)(s_l1_tx_seq & 0xFFu);
    s_l1_tx_seq++;

    out[PROTO_L1_HDR_LEN + 0u] = cmd;
    out[PROTO_L1_HDR_LEN + 1u] = (uint8_t)((version & 0x0Fu) << 4);
    out[PROTO_L1_HDR_LEN + 2u] = key;
    out[PROTO_L1_HDR_LEN + 3u] = (uint8_t)(len >> 8);
    out[PROTO_L1_HDR_LEN + 4u] = (uint8_t)(len & 0xFFu);
    if (len > 0)
    {
        memcpy(out + PROTO_L1_HDR_LEN + PROTO_L2_HDR_LEN, value, len);
    }

    crc = proto_crc16(out + PROTO_L1_HDR_LEN, (uint16_t)(PROTO_L2_HDR_LEN + len));
    out[HDR_OFF_CRC_HI] = (uint8_t)(crc >> 8);
    out[HDR_OFF_CRC_LO] = (uint8_t)(crc & 0xFFu);

    return total;
}

bool proto_send_l2(uint8_t cmd, uint8_t version, uint8_t key,
                   const uint8_t *value, uint16_t len)
{
    uint8_t  frame[PROTO_L1_MAX_FRAME];
    uint16_t total = proto_l2_build(cmd, version, key, value, len, frame, sizeof(frame));

    if (total == 0 || s_tx == NULL)
    {
        return false;
    }
    s_tx(frame, total);
    return true;
}
