/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include "wifi_ctrl.h"
#include "../protocol/wifi_atcmd.h"
#include "../transport/wifi_uart.h"
#include "../core/wifi_types.h"
#include "../core/wifi_task.h"

#define PARAM_BUF_SIZE  64

static bool ctrl_send(T_ATCMD_TYPE cmd, const char *param, T_ATCMD_RSP_CB cb)
{
    if (!wifi_atcmd_enqueue(cmd, param, cb))
    {
        return false;
    }
    T_WIFI_MSG msg = {.event = EVENT_UART_CMD_FLOW_CTRL};
    return wifi_task_send_msg(&msg);
}

/* replace_ip_address 改为 static，不对外暴露 */
static int replace_ip_in_cmd(const char *cmd_str, const char *new_ip,
                             char *output, size_t output_size)
{
    if (!cmd_str || !new_ip || !output || output_size == 0)
    {
        return -1;
    }
    const char *ip_start = strstr(cmd_str, "-c,");
    if (!ip_start)
    {
        return -1;
    }
    ip_start += 3;

    const char *ip_end = strchr(ip_start, ',');
    if (!ip_end)
    {
        ip_end = cmd_str + strlen(cmd_str);
    }

    size_t prefix_len = (size_t)(ip_start - cmd_str);
    size_t new_ip_len = strlen(new_ip);
    size_t suffix_len = strlen(ip_end);
    size_t total      = prefix_len + new_ip_len + suffix_len;

    if (total >= output_size)
    {
        return -1;
    }

    memcpy(output, cmd_str, prefix_len);
    memcpy(output + prefix_len, new_ip, new_ip_len);
    memcpy(output + prefix_len + new_ip_len, ip_end, suffix_len);
    output[total] = '\0';
    return 0;
}

void wifi_ctrl_init(void)
{
    wifi_uart_init();
    wifi_atcmd_init();
}

bool wifi_ctrl_scan(T_ATCMD_RSP_CB cb)
{
    return ctrl_send(ATCMD_ATWS, NULL, cb);
}

bool wifi_ctrl_connect(const char *ssid, const char *passwd, T_ATCMD_RSP_CB cb)
{
    char param[PARAM_BUF_SIZE];
    snprintf(param, sizeof(param), "%s,%s", ssid ? ssid : "", passwd ? passwd : "");
    return ctrl_send(ATCMD_ATPN, param, cb);
}

bool wifi_ctrl_disconnect(T_ATCMD_RSP_CB cb)
{
    return ctrl_send(ATCMD_ATW1, NULL, cb);
}


bool wifi_ctrl_tcp_open(const char *ip, uint16_t port, bool is_server, T_ATCMD_RSP_CB cb)
{
    if (is_server)
    {
        char param[PARAM_BUF_SIZE];
        snprintf(param, sizeof(param), "-s,-p,%u", port);
        return ctrl_send(ATCMD_ATWT, param, cb);
    }

    char tmpl[]        = "-c,192.168.0.0,-i,1,-t,3600";
    char param[PARAM_BUF_SIZE];
    char ip_str[20];

    snprintf(ip_str, sizeof(ip_str), "%s", ip ? ip : "192.168.0.0");

    if (replace_ip_in_cmd(tmpl, ip_str, param, sizeof(param)) != 0)
    {
        printf("[ctrl] tcp_open ip replace fail\n");
        return false;
    }
    return ctrl_send(ATCMD_ATWT, param, cb);
}

bool wifi_ctrl_info(T_ATCMD_RSP_CB cb)
{
    return ctrl_send(ATCMD_ATWQ, NULL, cb);
}

bool wifi_ctrl_sleep(T_ATCMD_RSP_CB cb)
{
    return ctrl_send(ATCMD_ATSL, "r[0]", cb);
}
