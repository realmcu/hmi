/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_TYPES_H_
#define _WIFI_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    EVENT_UART_RX            = 0, /* UART ISR: AT CMD 响应数据到达 */
    EVENT_UART_CMD_FLOW_CTRL = 1, /* AT CMD 状态机: 发下一条命令 */
    EVENT_SDIO_INT           = 2, /* GPIO ISR: WiFi 模块 RX 或状态变化 */
    EVENT_SDIO_TX_DRAIN      = 3, /* 驱动写队列非阻塞排空(事件驱动替代 while+delay) */
    EVENT_USER_APP_DEFINE    = 4, /* 用户自定义回调(测试/调试保留) */
} T_WIFI_EVENT;

typedef struct t_wifi_msg T_WIFI_MSG;
typedef void (*wifi_msg_cb)(T_WIFI_MSG *p_msg);

struct t_wifi_msg
{
    uint16_t     event;
    uint16_t     subtype;
    wifi_msg_cb  msg_cb;
    union
    {
        uint32_t param;
        void    *buf;
    } u;
};

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TYPES_H_ */
