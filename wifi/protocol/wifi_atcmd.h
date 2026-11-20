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
    ATCMD_ATW0,  /* set the SSID of the AP to join:   ATW0=ssid */
    ATCMD_ATW1,  /* set the password of the AP:       ATW1=password */
    ATCMD_ATWC,  /* connect to the AP after ATW0/ATW1: ATWC */
    ATCMD_ATWI,  /* Ping:            ATWI=ip */
    ATCMD_ATWU,  /* UDP iperf throughput:  ATWU=-s/-c,host,... */
    ATCMD_ATWT,  /* TCP iperf throughput:  ATWT=-s/-c,host,... */
    ATCMD_ATPN,  /* Connect AP: ATPN=ssid,password */
    ATCMD_ATSL,  /* Sleep mode */
    ATCMD_ATWO,  /* WiFi OTA */
    ATCMD_ATSD,  /* SD card */
    ATCMD_ATST,  /* WiFi test */
    ATCMD_ATWQ,  /* WiFi info: IP/MAC/GW (ATW?) */
    ATCMD_NUM,
} T_ATCMD_TYPE;

/* Callback for an AT response line; rsp_line is a '\0'-terminated string */
typedef bool (*T_ATCMD_RSP_CB)(T_ATCMD_TYPE cmd, const char *rsp_line);

/* Initialize the command table, timer and queue */
void wifi_atcmd_init(void);

/* Append an AT CMD to the send queue; param may be NULL */
bool wifi_atcmd_enqueue(T_ATCMD_TYPE cmd, const char *param, T_ATCMD_RSP_CB cb);

/* Handle EVENT_UART_RX: read the ring buffer, parse a line, fire callbacks, advance flow */
void wifi_atcmd_rx_handler(void);

/* Handle EVENT_UART_CMD_FLOW_CTRL: pop the next command from the queue and send it */
void wifi_atcmd_flow_ctrl_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_PROTOCOL_ATCMD_H_ */
