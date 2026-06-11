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
    WIFI_UART_MODE_AT, /* AT CMD 控制模式（默认）*/
    WIFI_UART_MODE_MP, /* 固件烧录 MP 模式 */
} T_WIFI_UART_MODE;

/* 初始化 UART，注册 async callback，启动 DMA 接收 */
int wifi_uart_init(void);

/* 切换 AT/MP 模式（pinctrl + buf_index 重置）*/
int wifi_uart_set_mode(T_WIFI_UART_MODE mode);

/* 切换波特率并重新使能接收 */
int wifi_uart_set_baudrate(uint32_t baud);

/* 非阻塞发送（内部持有 tx_lock）*/
bool wifi_uart_tx(const uint8_t *data, uint16_t len);

/* 从 ring buffer 读取最多 max_len 字节，返回实际读出字节数
 * AT 模式和 MP 模式共用此函数，ISR 安全 */
uint16_t wifi_uart_rx_read(uint8_t *buf, uint16_t max_len);

/* 查询 ring buffer 中当前可读字节数（不消费）*/
uint16_t wifi_uart_rx_avail(void);

/* MP 模式专用：阻塞等待接收数据（信号量 + timeout_ms）
 * 返回实际读出字节数，0 表示超时 */
uint16_t wifi_uart_mp_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TRANSPORT_UART_H_ */
