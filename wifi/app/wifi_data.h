/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_APP_DATA_H_
#define _WIFI_APP_DATA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Callback when RX data arrives */
typedef uint16_t (*T_WIFI_DATA_RX_CB)(uint32_t ip_addr, uint16_t port,
                                      const void *data, uint16_t len);

/* Register an RX callback.
 * Note: current RX frames carry no ip/port, so wifi_data_sdio_rx_handler looks up callbacks
 * with a wildcard (ip=0,port=0); a handler registered with a non-wildcard address will never
 * match RX. The parameter is reserved for future address-based routing. Register with (0,0) to receive data. */
bool wifi_data_rx_register(uint32_t ip_addr, uint16_t port, T_WIFI_DATA_RX_CB cb);
bool wifi_data_rx_unregister(uint32_t ip_addr, uint16_t port);

/* Asynchronous send: enqueue into the write queue and return immediately; sending is driven by EVENT_SDIO_TX_DRAIN */
bool wifi_data_tx(uint32_t ip_addr, uint16_t port, const uint8_t *data, uint16_t len);

/* Called by wifi_task: EVENT_SDIO_INT handling (read frame -> route to callback by IP:Port) */
void wifi_data_sdio_rx_handler(void);

/* Called by wifi_task: EVENT_SDIO_TX_DRAIN handling (non-blocking single frame; retry on a timer if BDs are exhausted) */
void wifi_data_sdio_tx_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_APP_DATA_H_ */
