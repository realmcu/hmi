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
#include "gui_server.h"
#include "rtl876x_pinmux.h"
#include "app_lower_init.h"


#define ENABLE_STREAM

#ifdef ENABLE_STREAM  // streaming
#include "gui_stream.h"
#define DSP_RSV_SIZE (512 * 1024)
#define STREAM_DB  (void *)(0x22000000 + DSP_RSV_SIZE + 0x200000)
#define STREAM_SIZE  0x100000u
#define MAX_FRAME       (25 * 1024u)   /* per-buffer cap (>> any real frame) */
#define POOL_BUFS       30              /* FIFO depth per stream              */
typedef struct
{
    // avi_info_t       info;
    stp_transport_t *tp;
    uint8_t         *pool;
    uint32_t         pool_size;
    uint32_t         interval_ms;
    const char      *label;
    volatile bool    running;
} demo_stream_t;
#endif



static void stream_prepare(void)
{
#ifdef ENABLE_STREAM  // streaming
    extern demo_stream_t s_stream_bt;
    s_stream_bt.tp          = NULL;
    s_stream_bt.pool        = STREAM_DB;
    s_stream_bt.pool_size   = STREAM_SIZE;
    s_stream_bt.label       = NULL;
    s_stream_bt.interval_ms = 46;

    static const stp_class_cfg_t classes[] =
    {
        { .buf_size = MAX_FRAME, .buf_count = POOL_BUFS },
    };
    stp_config_t cfg;
    stp_config_default(&cfg);
    cfg.pool        = s_stream_bt.pool;
    cfg.pool_size   = s_stream_bt.pool_size;
    cfg.align       = 8;
    cfg.classes     = classes;
    cfg.class_count = 1;
    cfg.drop_mode   = STP_DROP_NONE;

    s_stream_bt.tp = stp_create(&cfg);
    if (!s_stream_bt.tp)
    {
        printf("stream demo: stp_create failed\n");
        return ;
    }
    printf("stream demo: stp_create done\n");
#endif
}

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
    hmi_proto_task_init();
    extern void hmi_l2_handlers_register(void);
    hmi_l2_handlers_register();


    extern void rtk_lcd_hal_init(void);
    rtk_lcd_hal_init();




    stream_prepare();



    gui_server_init();

    return 0;
}
