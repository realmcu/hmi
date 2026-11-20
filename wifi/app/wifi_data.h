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

/* Callback for incoming RX data */
typedef uint16_t (*T_WIFI_DATA_RX_CB)(uint32_t ip_addr, uint16_t port,
                                      const void *data, uint16_t len);

/* Register an RX callback.
 * Note: RX frames carry no ip/port, so wifi_data_sdio_rx_handler always looks up the
 * callback by wildcard (ip=0,port=0); a non-wildcard registration never matches RX.
 * Register with (0,0) to receive data; the parameter only reserves address routing. */
bool wifi_data_rx_register(uint32_t ip_addr, uint16_t port, T_WIFI_DATA_RX_CB cb);
bool wifi_data_rx_unregister(uint32_t ip_addr, uint16_t port);

/* Async send: enqueue the packet and return; draining is driven by EVENT_SDIO_TX_DRAIN */
bool wifi_data_tx(uint32_t ip_addr, uint16_t port, const uint8_t *data, uint16_t len);

/* Called by wifi_task: handles EVENT_SDIO_INT (read frame -> route by IP:Port) */
void wifi_data_sdio_rx_handler(void);

/* Called by wifi_task: handles EVENT_SDIO_TX_DRAIN (one non-blocking frame, retry later) */
void wifi_data_sdio_tx_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_APP_DATA_H_ */
