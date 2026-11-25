/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include "utils.h"
#include "trace.h"

#include "drv_gpio.h"

// DLPS
static bool GUI_ALLOW_DLPS = false;

void gui_dlps_set(bool val)
{
    GUI_ALLOW_DLPS = val;
}

bool gui_dlps_check(void)
{
#if (DLPS_EN == 1)
    return GUI_ALLOW_DLPS;
#else
    return false;
#endif
}

static bool gui_dlps_enter(void)
{
    /* Do NOT delay here. */
    rtk_touch_hal_power_off();
    rtk_lcd_hal_power_off();

    return true;
}

static bool gui_dlps_exit(void)
{
    return true;
}

static bool gui_dlps_wake_up(void)
{
    if (rtk_touch_wake_up())
    {
        // hw wake-up
        rtk_touch_hal_power_on();

        // psram
        uint32_t actual_mhz = 160;
        fmc_psram_clock_switch(PSRAM_IDX_SPIC1, 160, &actual_mhz);
        psram_winbond_opi_init();
        pm_display_freq_set(CLK_PLL1_SRC, 100, 100);

        // lcd
        rtk_lcd_hal_power_on();

        // sw wake-up
        gui_dlps_set(false);
        lvgl_wake_up();

    }
    return true;
}

/*
    dlps configuration
    register cb
*/
void gui_dlps_init(void)
{
#if (DLPS_EN == 1)
    drv_dlps_check_cbacks_register("gui", gui_dlps_check);
    drv_dlps_enter_cbacks_register("gui", gui_dlps_enter);
    drv_dlps_exit_cbacks_register("gui", gui_dlps_exit);
    drv_dlps_wakeup_cbacks_register("gui", gui_dlps_wake_up);
#else

#endif
}

