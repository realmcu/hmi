/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>
#include "trace.h"
#include "rtl876x_pinmux.h"
#include "app_lower_init.h"
#include "app_core.h"
#include "app_ota_mode.h"
#include "hmi_bt_task.h"

/* Boot messages go to both the console and the trace stream. */
#define BOOT_LOG(fmt, ...)                  \
    do {                                    \
        printf(fmt "\n", ##__VA_ARGS__);    \
        DBG_DIRECT(fmt, ##__VA_ARGS__);     \
    } while (0)

int main(void)
{
    app_system_lower_init();

#if CONFIG_DFU_NORMAL_OTA
    if (app_ota_mode_requested())
    {
        BOOT_LOG("[OTA] enter dedicated normal OTA mode");
        app_ota_mode_clear_request();
        app_ota_mode_set_active(true);
        if (hmi_bt_task_start_ota_mode())
        {
            return 0;
        }
        BOOT_LOG("[OTA] failed to start OTA task");
    }
#endif

    BOOT_LOG("Hello World! %s", CONFIG_BOARD_TARGET);
    BOOT_LOG("[APP] build: %s %s", __DATE__, __TIME__);
    BOOT_LOG("[main] thread id: 0x%08x, name: %s, priority: %d",
             (unsigned)(uintptr_t)k_current_get(),
             k_thread_name_get(k_current_get()),
             k_thread_priority_get(k_current_get()));

    app_core_init();

    return 0;
}
