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
    WIFI_UART_MODE_MP, /* Firmware flashing MP mode */
} T_WIFI_UART_MODE;

/* Initialize UART, register the async callback, and start DMA reception */
int wifi_uart_init(void);

/* Switch between AT/MP mode (pinctrl + buf_index reset) */
int wifi_uart_set_mode(T_WIFI_UART_MODE mode);

/* Switch the baud rate and re-enable reception */
int wifi_uart_set_baudrate(uint32_t baud);

/* Non-blocking send (holds tx_lock internally) */
bool wifi_uart_tx(const uint8_t *data, uint16_t len);

/* Read up to max_len bytes from the ring buffer and return the actual read count
 * Shared by AT and MP modes; ISR-safe */
uint16_t wifi_uart_rx_read(uint8_t *buf, uint16_t max_len);

/* Query the current readable byte count in the ring buffer (without consuming) */
uint16_t wifi_uart_rx_avail(void);

/* MP mode only: block waiting for received data (semaphore + timeout_ms)
 * Returns the actual read count; 0 means timeout */
uint16_t wifi_uart_mp_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TRANSPORT_UART_H_ */
