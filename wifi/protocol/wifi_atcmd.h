/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_PROTOCOL_ATCMD_H_
#define _WIFI_PROTOCOL_ATCMD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    ATCMD_ATWS,  /* WiFi scan */
    ATCMD_ATW0,  /* WiFi connect (open) */
    ATCMD_ATW1,  /* WiFi disconnect */
    ATCMD_ATWC,  /* WiFi connect */
    ATCMD_ATWT,  /* WiFi TCP transfer */
    ATCMD_ATPN,  /* Ping */
    ATCMD_ATSL,  /* Sleep mode */
    ATCMD_ATWO,  /* WiFi OTA */
    ATCMD_ATSD,  /* SD card */
    ATCMD_ATST,  /* WiFi test */
    ATCMD_ATWQ,  /* WiFi info: IP/MAC/GW (ATW?) */
    ATCMD_NUM,
} T_ATCMD_TYPE;

/* 收到 AT 响应行时的回调，rsp_line 为 '\0' 结尾字符串 */
typedef bool (*T_ATCMD_RSP_CB)(T_ATCMD_TYPE cmd, const char *rsp_line);

/* 初始化命令表、定时器、队列 */
void wifi_atcmd_init(void);

/* 将一条 AT CMD 加入发送队列；param 可为 NULL */
bool wifi_atcmd_enqueue(T_ATCMD_TYPE cmd, const char *param, T_ATCMD_RSP_CB cb);

/* 处理 EVENT_UART_RX：读 ring buffer、解析响应行、触发回调、推进流控 */
void wifi_atcmd_rx_handler(void);

/* 处理 EVENT_UART_CMD_FLOW_CTRL：从队列取下一条命令发送 */
void wifi_atcmd_flow_ctrl_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_PROTOCOL_ATCMD_H_ */
