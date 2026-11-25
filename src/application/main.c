/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "trace.h"
#include "os_sched.h"
#include "lvgl_init.h"

/* Non-Secure main. */
int main(void)
{
    DBG_DIRECT("Non-Secure World: main");
    lvgl_system_clock_init();
    lvgl_components_init();
    lvgl_task_init();

    /* Start scheduler. */
    os_sched_start();
}
/*-----------------------------------------------------------*/
