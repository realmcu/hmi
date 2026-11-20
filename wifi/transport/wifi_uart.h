/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_TRANSPORT_UART_H_
#define _WIFI_TRANSPORT_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    WIFI_UART_MODE_AT, /* AT CMD control mode (default) */
    WIFI_UART_MODE_MP, /* MP mode for firmware flashing */
} T_WIFI_UART_MODE;

/* Initialize the UART, register the async callback and start DMA receive */
int wifi_uart_init(void);

/* Switch AT/MP mode (pinctrl + buf_index reset) */
int wifi_uart_set_mode(T_WIFI_UART_MODE mode);

/* Change the baud rate and re-enable reception */
int wifi_uart_set_baudrate(uint32_t baud);

/* Non-blocking send (takes tx_lock internally) */
bool wifi_uart_tx(const uint8_t *data, uint16_t len);

/* Read at most max_len bytes from the ring buffer; returns the bytes actually read
 * Shared by AT and MP mode; ISR-safe */
uint16_t wifi_uart_rx_read(uint8_t *buf, uint16_t max_len);

/* Query the number of readable bytes in the ring buffer (non-consuming) */
uint16_t wifi_uart_rx_avail(void);

/* MP mode only: block waiting for received data (semaphore + timeout_ms)
 * Returns the bytes actually read; 0 means timeout */
uint16_t wifi_uart_mp_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TRANSPORT_UART_H_ */
