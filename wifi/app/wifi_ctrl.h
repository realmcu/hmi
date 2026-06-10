/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_APP_CTRL_H_
#define _WIFI_APP_CTRL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "../protocol/wifi_atcmd.h"

/* WiFi 控制 API — 每个函数将对应的 AT CMD 入队并触发流控
 * cb 在响应到来时被调用，可为 NULL */

bool wifi_ctrl_connect(const char *ssid, const char *passwd, T_ATCMD_RSP_CB cb);
bool wifi_ctrl_disconnect(T_ATCMD_RSP_CB cb);
bool wifi_ctrl_scan(T_ATCMD_RSP_CB cb);
bool wifi_ctrl_tcp_open(const char *ip, uint16_t port, bool is_server, T_ATCMD_RSP_CB cb);
bool wifi_ctrl_info(T_ATCMD_RSP_CB cb);
bool wifi_ctrl_sleep(T_ATCMD_RSP_CB cb);

/* 初始化控制层（调用 wifi_atcmd_init 和 wifi_uart_init）*/
void wifi_ctrl_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_APP_CTRL_H_ */
