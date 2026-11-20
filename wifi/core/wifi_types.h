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
    EVENT_UART_RX            = 0, /* UART ISR: AT CMD response data arrived */
    EVENT_UART_CMD_FLOW_CTRL = 1, /* AT CMD state machine: send the next command */
    EVENT_SDIO_INT           = 2, /* GPIO ISR: WiFi module RX or status change */
    EVENT_SDIO_TX_DRAIN      = 3, /* non-blocking write-queue drain (event-driven) */
    EVENT_USER_APP_DEFINE    = 4, /* user-defined callback (reserved for test/debug) */
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
