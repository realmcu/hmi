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

/* RX 数据到达时的回调 */
typedef uint16_t (*T_WIFI_DATA_RX_CB)(uint32_t ip_addr, uint16_t port,
                                      const void *data, uint16_t len);

/* 注册 RX 回调。
 * 注意：当前 RX 帧不携带 ip/port，wifi_data_sdio_rx_handler 统一以通配
 * (ip=0,port=0) 查找回调；以非通配地址注册的 handler 不会被 RX 命中，
 * 该参数仅为将来支持带地址路由预留。需要接收数据时请用 (0,0) 注册。*/
bool wifi_data_rx_register(uint32_t ip_addr, uint16_t port, T_WIFI_DATA_RX_CB cb);
bool wifi_data_rx_unregister(uint32_t ip_addr, uint16_t port);

/* 异步发送：打包入写队列立即返回；发送由 EVENT_SDIO_TX_DRAIN 驱动 */
bool wifi_data_tx(uint32_t ip_addr, uint16_t port, const uint8_t *data, uint16_t len);

/* 由 wifi_task 调用：EVENT_SDIO_INT 处理（读帧 → 按 IP:Port 路由到回调）*/
void wifi_data_sdio_rx_handler(void);

/* 由 wifi_task 调用：EVENT_SDIO_TX_DRAIN 处理（非阻塞单帧，BD 不足则定时重试）*/
void wifi_data_sdio_tx_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_APP_DATA_H_ */
