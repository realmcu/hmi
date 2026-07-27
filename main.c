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
#include "hmi_protocal_task.h"
#include "hmi_ble_ctrl.h"      /* proto transport: hmi_ble_ctrl_send / _receive */
#include "gui_server.h"
#include "rtl876x_pinmux.h"
#include "app_lower_init.h"

int main(void)
{

#ifndef CONFIG_UART_CONSOLE
    DBG_DIRECT("!!!!! remap log pin!!!");
    Pinmux_Config(P2_0, IDLE_MODE);
    Pinmux_Config(P3_1, UART1_TX);
    Pad_Config(P3_1, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_LOW);
    DBG_DIRECT("!!!!! remap log pin!!!");
#endif
    app_system_lower_init();
    printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
    printf("[main] thread id: %p, name: %s, priority: %d\n",
           k_current_get(),
           k_thread_name_get(k_current_get()),
           k_thread_priority_get(k_current_get()));

    hmi_bt_task_init();
    /* proto is transport-agnostic now: inject the BLE peripheral transport. */
    hmi_proto_task_init(hmi_ble_ctrl_send, hmi_ble_ctrl_receive);
    extern void hmi_l2_handlers_register(void);
    hmi_l2_handlers_register();


    // extern void rtk_lcd_hal_init(void);
    // rtk_lcd_hal_init();



    gui_server_init();
    gui_set_keep_active_time(0xFFFFFFFF);

    return 0;
}
