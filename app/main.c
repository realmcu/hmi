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
#ifdef ENABLE_HONEYGUI
#include "gui_server.h"
#endif

#include "string.h"





int main(void)
{
    APP_PRINT_INFO1("main function line = %d!", __LINE__);

    system_lower_init();

    extern void rtk_lcd_hal_init(void);
    rtk_lcd_hal_init();


#ifdef ENABLE_HONEYGUI
    app_task_init();
    gui_set_keep_active_time(1000000);
#ifdef __cplusplus
    {
        typedef void PROC();
        extern const unsigned long SHT$$INIT_ARRAY$$Base[];
        extern const unsigned long SHT$$INIT_ARRAY$$Limit[];

        const unsigned long *base = SHT$$INIT_ARRAY$$Base;
        const unsigned long *lim  = SHT$$INIT_ARRAY$$Limit;

        for (; base != lim; base++)
        {
            PROC *proc = (PROC *)((const char *)base + *base);
            (*proc)();
        }
    }
#endif
#endif

    extern void hmi_bt_task_init(void);
    extern void hmi_proto_task_init(void);
    extern void hmi_l2_handlers_register(void);
    hmi_bt_task_init();
    hmi_proto_task_init();
    hmi_l2_handlers_register();

    os_sched_start();
    while (1);

}
