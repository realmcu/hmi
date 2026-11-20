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

/* IO injection: bound to wifi_uart_tx / wifi_uart_mp_recv in production */
typedef bool (*xmodem_tx_fn)(const uint8_t *data, uint16_t len);
typedef uint16_t (*xmodem_rx_fn)(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

/* Initialize the session, bind IO, reset the sequence number */
void wifi_xmodem_init(xmodem_tx_fn tx, xmodem_rx_fn rx);

/* Send data (fragmented into 1024-byte packets + padding + checksum)
 * Bug 4 fix: EOT is not sent here */
T_XMODEM_STATUS wifi_xmodem_send(const uint8_t *data, uint32_t len);

/* Send EOT and wait for ACK (called exactly once after the whole image) */
T_XMODEM_STATUS wifi_xmodem_finish(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_PROTOCOL_XMODEM_H_ */
