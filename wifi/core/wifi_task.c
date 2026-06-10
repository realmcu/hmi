/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include "wifi_task.h"
#include "wifi_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtl876x_pinmux.h"
#include "wdg.h"
#include <os_msg.h>
#include <os_task.h>
#include <os_sched.h>

#include "wifi_atcmd.h"
#include "wifi_data.h"
#include "wifi_ctrl.h"
#include "wifi_sdio.h"

#define WIFI_EN_PIN     P6_4
#define RF_SWITCH_V2    P6_2
#define RF_SWITCH_V1    P6_0

static void *wifi_msg_queue_handle;
static void *wifi_task_handle;

static void wifi_task_loop(void *param);
static uint16_t on_sdio_rx(uint32_t ip, uint16_t port, const void *data, uint16_t len);

bool wifi_task_send_msg(T_WIFI_MSG *p_msg)
{
    if (os_msg_send(wifi_msg_queue_handle, p_msg, 0) == false)
    {
        printf("[wifi] send_msg fail!\n");
        return false;
    }
    return true;
}

void wifi_enable(bool enable)
{
    if (enable)
    {
        printf("[wifi] enable\n");
        /* 先拉低再拉高确保复位时序 */
        Pad_Config(WIFI_EN_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_LOW);
        os_delay(200);
        Pad_Config(WIFI_EN_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
        os_delay(2000);

        Pad_Config(RF_SWITCH_V1, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE,
                   PAD_OUT_HIGH);
        Pinmux_Config(RF_SWITCH_V1, EN_EXLNA);
        Pad_Config(RF_SWITCH_V2, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE,
                   PAD_OUT_HIGH);
        Pinmux_Config(RF_SWITCH_V2, EN_EXPA);
    }
    else
    {
        printf("[wifi] disable\n");
        Pad_Config(WIFI_EN_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_LOW);
    }
}

static void wifi_task_loop(void *param)
{
    T_WIFI_MSG wifi_msg;

    while (1)
    {
        wdg_kick();
        if (os_msg_recv(wifi_msg_queue_handle, &wifi_msg, 0xFFFFFFFF) == false)
        {
            continue;
        }

        switch (wifi_msg.event)
        {
        case EVENT_UART_RX:
            wifi_atcmd_rx_handler();
            break;
        case EVENT_UART_CMD_FLOW_CTRL:
            wifi_atcmd_flow_ctrl_handler();
            break;
        case EVENT_SDIO_INT:
            wifi_data_sdio_rx_handler();
            break;
        case EVENT_SDIO_TX_DRAIN:
            wifi_data_sdio_tx_handler();
            break;
        case EVENT_USER_APP_DEFINE:
            if (wifi_msg.msg_cb)
            {
                wifi_msg.msg_cb(&wifi_msg);
            }
            break;
        default:
            printf("[wifi] unknown event: %d\n", wifi_msg.event);
            break;
        }
    }
}

/* ---- shell commands ---- */

static bool on_scan_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    printk("[wifi] ATWS rsp: %s\n", rsp);
    return true;
}

static int cmd_wifi_start(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "[wifi] enable...");
    wifi_enable(true);
    shell_print(sh, "[wifi] sdio_init...");
    int ret = wifi_sdio_init();
    shell_print(sh, "[wifi] sdio_init ret=%d", ret);
    shell_print(sh, "[wifi] ctrl_init...");
    wifi_ctrl_init();
    shell_print(sh, "[wifi] scan...");
    wifi_ctrl_scan(on_scan_rsp);
    shell_print(sh, "[wifi] start done, waiting ATWS response");
    return 0;
}

static int cmd_wifi_scan(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "[wifi] scan");
    wifi_ctrl_scan(on_scan_rsp);
    return 0;
}

static int cmd_wifi_stop(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "[wifi] stop");
    wifi_enable(false);
    return 0;
}

static bool on_info_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    printk("[wifi] info: %s\n", rsp);
    return true;
}

static int cmd_wifi_info(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "[wifi] querying IP info...");
    wifi_ctrl_info(on_info_rsp);
    return 0;
}

static bool on_connect_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    printk("[wifi] ATWC rsp: %s\n", rsp);
    return true;
}

static int cmd_wifi_connect(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_print(sh, "Usage: wifi connect <ssid> [password]");
        return -EINVAL;
    }
    const char *ssid   = argv[1];
    const char *passwd = (argc >= 3) ? argv[2] : "";
    shell_print(sh, "[wifi] connecting to %s ...", ssid);
    wifi_ctrl_connect(ssid, passwd, on_connect_rsp);
    return 0;
}

static bool on_disconnect_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    printk("[wifi] ATW1 rsp: %s\n", rsp);
    return true;
}

static int cmd_wifi_disconnect(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "[wifi] disconnecting...");
    wifi_ctrl_disconnect(on_disconnect_rsp);
    return 0;
}


static bool on_tcp_open_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    printk("[wifi] tcp_open rsp: %s\n", rsp);
    return true;
}

static int cmd_wifi_tcp_open(const struct shell *sh, size_t argc, char **argv)
{
    /* Usage:
     *   wifi tcp_open <ip> <port>    client: 连接到 ip:port
     *   wifi tcp_open -s <port>      server: 监听 port，等待 PC 连入
     */
    if (argc < 3)
    {
        shell_print(sh, "Usage: wifi tcp_open <ip> <port>  (client)");
        shell_print(sh, "       wifi tcp_open -s <port>    (server)");
        return -EINVAL;
    }

    bool is_server = (strcmp(argv[1], "-s") == 0);
    uint16_t port  = (uint16_t)atoi(argv[2]);

    /* RX 帧不带 ip/port，统一用通配 handler 接收对端数据 */
    wifi_data_rx_register(0, 0, on_sdio_rx);

    if (is_server)
    {
        shell_print(sh, "[wifi] tcp server listening on port %d ...", port);
        wifi_ctrl_tcp_open(NULL, port, true, on_tcp_open_rsp);
    }
    else
    {
        shell_print(sh, "[wifi] tcp client connecting to %s:%d ...", argv[1], port);
        wifi_ctrl_tcp_open(argv[1], port, false, on_tcp_open_rsp);
    }
    return 0;
}

static uint16_t on_sdio_rx(uint32_t ip, uint16_t port, const void *data, uint16_t len)
{
    printk("[wifi] sdio rx len=%d: %.*s\n", len, len, (const char *)data);
    return len;
}

static int cmd_wifi_tx(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_print(sh, "Usage: wifi tx <message>");
        return -EINVAL;
    }
    const char *msg = argv[1];
    bool ok = wifi_data_tx(0, 0, (const uint8_t *)msg, (uint16_t)strlen(msg));
    shell_print(sh, "[wifi] tx msg=\"%s\" ret=%d", msg, ok);
    return 0;
}

static bool on_ping_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    printk("[wifi] ping: %s\n", rsp);
    return true;
}

static int cmd_wifi_ping(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_print(sh, "Usage: wifi ping <ip>");
        return -EINVAL;
    }
    shell_print(sh, "[wifi] ping %s ...", argv[1]);
    wifi_ctrl_ping(argv[1], on_ping_rsp);
    return 0;
}

static bool on_iperf_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    printk("[wifi] iperf: %s\n", rsp);
    return true;
}

static int cmd_wifi_iperf(const struct shell *sh, size_t argc, char **argv)
{
    /* Usage:
     *   wifi iperf udp <args>   例: wifi iperf udp -s,-i,1
     *   wifi iperf tcp <args>   例: wifi iperf tcp -c,192.168.3.98,-n,10m,-i,1
     */
    if (argc < 3)
    {
        shell_print(sh, "Usage: wifi iperf <udp|tcp> <args>");
        shell_print(sh, "  e.g. wifi iperf udp -s,-i,1");
        shell_print(sh, "       wifi iperf tcp -c,192.168.3.98,-n,10m,-i,1");
        return -EINVAL;
    }

    if (strcmp(argv[1], "udp") == 0)
    {
        wifi_ctrl_iperf_udp(argv[2], on_iperf_rsp);
    }
    else if (strcmp(argv[1], "tcp") == 0)
    {
        wifi_ctrl_iperf_tcp(argv[2], on_iperf_rsp);
    }
    else
    {
        shell_print(sh, "unknown proto: %s (use udp|tcp)", argv[1]);
        return -EINVAL;
    }
    shell_print(sh, "[wifi] iperf %s %s", argv[1], argv[2]);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(wifi_cmds,
                               SHELL_CMD(start,          NULL, "Enable WiFi and send first ATWS scan", cmd_wifi_start),
                               SHELL_CMD(info,           NULL, "Show device IP/MAC/GW",                cmd_wifi_info),
                               SHELL_CMD(scan,           NULL, "Send ATWS scan command",               cmd_wifi_scan),
                               SHELL_CMD(stop,           NULL, "Disable WiFi",                         cmd_wifi_stop),
                               SHELL_CMD_ARG(connect,    NULL, "Connect to AP: <ssid> [password]",     cmd_wifi_connect,    2, 1),
                               SHELL_CMD(disconnect,     NULL, "Disconnect from AP",                   cmd_wifi_disconnect),
                               SHELL_CMD_ARG(tcp_open,   NULL, "Open TCP to server: <ip> <port>",      cmd_wifi_tcp_open,   3, 0),
                               SHELL_CMD_ARG(tx,         NULL, "Send data: <message>",                 cmd_wifi_tx,         2, 0),
                               SHELL_CMD_ARG(ping,       NULL, "Ping IP: <ip>",                        cmd_wifi_ping,       2, 0),
                               SHELL_CMD_ARG(iperf,      NULL, "iperf throughput: <udp|tcp> <args>",   cmd_wifi_iperf,      3, 0),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(wifi, &wifi_cmds, "WiFi commands", NULL);

static int wifi_module_init(void)
{
    os_msg_queue_create(&wifi_msg_queue_handle, "wifi msg queue", 0x10, sizeof(T_WIFI_MSG));
    os_task_create(&wifi_task_handle, "wifi task", wifi_task_loop, NULL, 384 * 8, 2);
    return 0;
}
SYS_INIT(wifi_module_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
