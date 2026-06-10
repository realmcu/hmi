/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include <stdio.h>
#include "wifi_xmodem.h"
#include "os_sched.h"

#define XMODEM_PAYLOAD_SIZE  1024
#define XMODEM_PACKET_SIZE   (XMODEM_PAYLOAD_SIZE + 4)
#define XMODEM_RECV_TIMEOUT  1200
#define XMODEM_MAX_NAK       5

#define STX_XMODEM  0x02
#define EOT_XMODEM  0x04
#define ACK_XMODEM  0x06
#define NAK_XMODEM  0x15
#define PAD_XMODEM  0x1A

static xmodem_tx_fn s_tx  = NULL;
static xmodem_rx_fn s_rx  = NULL;
static uint8_t      s_sqn = 0;
static uint8_t      s_pkt_buf[XMODEM_PACKET_SIZE];

static uint8_t xmodem_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++)
    {
        sum += data[i];
    }
    return sum;
}

static void xmodem_build_packet(const uint8_t *data, uint16_t data_len)
{
    /* Bug 5 修复：序列号自增后跳过 0，永远不发 sqn=0 */
    s_sqn++;
    if (s_sqn == 0)
    {
        s_sqn = 1;
    }

    s_pkt_buf[0] = STX_XMODEM;
    s_pkt_buf[1] = s_sqn;
    s_pkt_buf[2] = (uint8_t)(0xFF - s_sqn);

    memcpy(&s_pkt_buf[3], data, data_len);
    if (data_len < XMODEM_PAYLOAD_SIZE)
    {
        memset(&s_pkt_buf[3 + data_len], PAD_XMODEM, XMODEM_PAYLOAD_SIZE - data_len);
    }

    s_pkt_buf[3 + XMODEM_PAYLOAD_SIZE] = xmodem_checksum(&s_pkt_buf[3], XMODEM_PAYLOAD_SIZE);
}

static T_XMODEM_STATUS xmodem_send_one_packet(const uint8_t *data, uint16_t data_len)
{
    uint8_t recv_byte = 0;
    uint8_t nak_cnt   = 0;

send_retry:
    xmodem_build_packet(data, data_len);
    if (!s_tx(s_pkt_buf, XMODEM_PACKET_SIZE))
    {
        return XMODEM_ERR_PROGRAM_FAIL;
    }

    uint16_t recv = s_rx(&recv_byte, 1, XMODEM_RECV_TIMEOUT);
    if (recv == 0)
    {
        return XMODEM_ERR_TIMEOUT;
    }

    if (recv_byte == ACK_XMODEM)
    {
        return XMODEM_OK;
    }
    if (recv_byte == NAK_XMODEM)
    {
        nak_cnt++;
        if (nak_cnt >= XMODEM_MAX_NAK)
        {
            printf("[xmodem] NAK exceeded\n");
            return XMODEM_ERR_NAK_EXCEEDED;
        }
        os_delay(10);
        /* 回退序列号以便重发时 build_packet 能重建相同 sqn */
        s_sqn--;
        if (s_sqn == 0)
        {
            s_sqn = 0xFF;
        }
        goto send_retry;
    }

    return XMODEM_ERR_PROGRAM_FAIL;
}

/* ---- 公开接口 ---- */

void wifi_xmodem_init(xmodem_tx_fn tx, xmodem_rx_fn rx)
{
    s_tx  = tx;
    s_rx  = rx;
    s_sqn = 0;
}

T_XMODEM_STATUS wifi_xmodem_send(const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0;

    while (offset < len)
    {
        uint32_t remain    = len - offset;
        uint16_t chunk_len = (remain >= XMODEM_PAYLOAD_SIZE)
                             ? XMODEM_PAYLOAD_SIZE : (uint16_t)remain;

        T_XMODEM_STATUS ret = xmodem_send_one_packet(data + offset, chunk_len);
        if (ret != XMODEM_OK)
        {
            printf("[xmodem] fail at offset=0x%x status=%d\n", offset, ret);
            return ret;
        }
        offset += chunk_len;
    }

    return XMODEM_OK;
}

/* Bug 4 修复：EOT 仅在此处发送一次，由 wifi_download_firmware() 在全部数据传完后调用 */
T_XMODEM_STATUS wifi_xmodem_finish(void)
{
    uint8_t eot = EOT_XMODEM;
    uint8_t ack = 0;

    if (!s_tx(&eot, 1))
    {
        return XMODEM_ERR_PROGRAM_FAIL;
    }

    uint16_t recv = s_rx(&ack, 1, XMODEM_RECV_TIMEOUT);
    if (recv == 0 || ack != ACK_XMODEM)
    {
        /* EOT 未被确认说明整包传输未被接收端确认，必须如实上报失败 */
        printf("[xmodem] finish: no ACK, recv=%d ack=0x%x\n", recv, ack);
        return XMODEM_ERR_TIMEOUT;
    }

    printf("[xmodem] finish ok\n");
    return XMODEM_OK;
}
