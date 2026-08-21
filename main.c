/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>
#include "trace.h"
#include "hmi_bt_task.h"
#include "ebadge_task.h"      /* V1.2 protocol stack entry point            */
#include "gui_server.h"
#include "rtl876x_pinmux.h"
#include "app_lower_init.h"
#if defined(CONFIG_WIFI_8711)
#include "wifi_8711.h"
#endif

int main(void)
{

#ifndef CONFIG_UART_CONSOLE
    DBG_DIRECT("!!!!! remap log pin!!!");
    Pinmux_Config(P2_0, IDLE_MODE);
    Pinmux_Config(P3_1, UART1_TX);
    Pad_Config(P3_1, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_LOW);
    DBG_DIRECT("!!!!! remap log pin!!!");
#endif

    // io init
    // WiFi VD33_WF_EN P0_2
    Pad_Config(ADC_2, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_HIGH);

    // BT ANT HIGHT
    Pad_Config(P2_6, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_HIGH);


    app_system_lower_init();
    printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
    printf("[main] thread id: %p, name: %s, priority: %d\n",
           k_current_get(),
           k_thread_name_get(k_current_get()),
           k_thread_priority_get(k_current_get()));

    hmi_bt_task_init();

    /* V1.2 protocol stack -- one call spins up:
     *   - l2_task (msg queue + tick timer)
     *   - port_ble (binds hmi_ctrl_service RX/TX/CCCD)
     *   - xfer_session (registers tick sink)
     *   - all 0x01..0x1A command handlers
     * See app/protocol/ebadge_task.c for the wiring order. */
    ebadge_task_init();

#if defined(CONFIG_WIFI_8711)
    /* SPI slave pads + B2W/W2B handshake toward the 8711FA.  Must come after
     * app_system_lower_init(): the DMA slot buffers live in SPIC1 PSRAM, which
     * only exists once that call has brought SPIC1 up.
     *
     * ADC_2 (VD33_WF_EN) was raised at the top of main(), so the 8711 has been
     * powered for the whole lower-init sequence by now.  How long its AT
     * firmware needs before it starts driving W2B is not yet characterised --
     * `wifi8711 info` reports the edge counters if it turns out to matter.
     *
     * Failure is deliberately non-fatal: the rest of the product (BLE, GUI)
     * works without Wi-Fi, and a missing 8711 must not cost us a boot. */
    {
        int wifi_rc = wifi_8711_init();
        if (wifi_rc != 0)
        {
            printf("[main] wifi_8711_init failed %d -- continuing without Wi-Fi\n",
                   wifi_rc);
        }
    }
#endif



    gui_server_init();
    gui_set_keep_active_time(0xFFFFFFFF);

    return 0;
}
