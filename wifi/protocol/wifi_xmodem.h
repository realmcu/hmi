/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_PROTOCOL_XMODEM_H_
#define _WIFI_PROTOCOL_XMODEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    XMODEM_OK = 0,
    XMODEM_ERR_TIMEOUT,
    XMODEM_ERR_PROGRAM_FAIL,
    XMODEM_ERR_NAK_EXCEEDED,
} T_XMODEM_STATUS;

/* IO 注入：生产时绑定 wifi_uart_tx / wifi_uart_mp_recv */
typedef bool (*xmodem_tx_fn)(const uint8_t *data, uint16_t len);
typedef uint16_t (*xmodem_rx_fn)(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

/* 初始化会话，绑定 IO，重置序列号 */
void wifi_xmodem_init(xmodem_tx_fn tx, xmodem_rx_fn rx);

/* 发送数据（按 1024 字节分包 + padding + checksum）
 * Bug 4 修复：EOT 不在此处发送 */
T_XMODEM_STATUS wifi_xmodem_send(const uint8_t *data, uint32_t len);

/* 发送 EOT，等待 ACK（整包传完后唯一调用一次）*/
T_XMODEM_STATUS wifi_xmodem_finish(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_PROTOCOL_XMODEM_H_ */
