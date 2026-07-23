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
#include "rtl876x_pinmux.h"
#include "app_lower_init.h"
#include "gui_server.h"
#include "app_core.h"




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
    printf("[APP] build: %s %s\n", __DATE__, __TIME__);
    printf("[main] thread id: %p, name: %s, priority: %d\n",
           k_current_get(),
           k_thread_name_get(k_current_get()),
           k_thread_priority_get(k_current_get()));
    gui_server_init();
    gui_set_keep_active_time(10000000);

    app_core_init();

    return 0;
}
