/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <stdio.h>
#include <trace.h>
#include "os_sched.h"
#include "app_panel_init.h"
#include "app_lower_init.h"
#include "app_rtc_time.h"
#include "hmi_bt_task.h"
#ifdef ENABLE_HONEYGUI
#include "gui_server.h"
#endif

int main(void)
{
    APP_PRINT_INFO1("main function line = %d!", __LINE__);

    system_lower_init();

    app_rtc_time_init();

    extern void rtk_lcd_hal_init(void);
    rtk_lcd_hal_init();

#ifdef ENABLE_HONEYGUI
    app_task_init();
    gui_set_keep_active_time(1000000);
#endif

    hmi_bt_task_init();

    os_sched_start();
    while (1);

}
