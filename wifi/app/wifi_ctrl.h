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
#include "wifi_atcmd.h"

/* WiFi control API -- each function queues the matching AT CMD and kicks flow control
 * cb is invoked when the response arrives; may be NULL */

bool wifi_ctrl_connect(const char *ssid, const char *passwd, T_ATCMD_RSP_CB cb);
bool wifi_ctrl_disconnect(T_ATCMD_RSP_CB cb);
bool wifi_ctrl_scan(T_ATCMD_RSP_CB cb);
bool wifi_ctrl_tcp_open(const char *ip, uint16_t port, bool is_server, T_ATCMD_RSP_CB cb);
bool wifi_ctrl_info(T_ATCMD_RSP_CB cb);
bool wifi_ctrl_sleep(T_ATCMD_RSP_CB cb);

/* Ping the given IP (maps to ATWI=ip) */
bool wifi_ctrl_ping(const char *ip, T_ATCMD_RSP_CB cb);

/* iperf throughput test: args is the parameter string passed through to ATWU/ATWT,
 * e.g. "-s,-i,1" (server) or "-c,192.168.3.98,-n,10m,-i,1" (client) */
bool wifi_ctrl_iperf_udp(const char *args, T_ATCMD_RSP_CB cb);
bool wifi_ctrl_iperf_tcp(const char *args, T_ATCMD_RSP_CB cb);

/* Initialize the control layer (calls wifi_atcmd_init and wifi_uart_init) */
void wifi_ctrl_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_APP_CTRL_H_ */
