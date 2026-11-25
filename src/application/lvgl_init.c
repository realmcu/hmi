/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "board.h"
#include "app_section.h"
#include "io_dlps.h"
#include "rtl876x.h"
#include "trace.h"
#include "rtl_nvic.h"
#include "rtl_gpio.h"
#include "rtl_rtc.h"
#include "utils.h"
#include "flash_nor_device.h"
#include "clock.h"
#include "lvgl_init.h"


void lvgl_task_init(void)
{

#ifdef RTK_BLE_PERIPHERAL_APP
    ble_peripheral_main();
#endif

#ifdef RTK_BLE_OTA_APP
    ble_ota_main();
#endif

    // lvgl task
    rt_lvgl_demo_init();
}

void lvgl_system_clock_init(void)
{
    uint32_t actual_mhz;
    int32_t ret0, ret1, ret2, ret3;
    ret0 = pm_cpu_freq_set(125, &actual_mhz);

    ret1 = flash_nor_set_seq_trans_enable(FLASH_NOR_IDX_SPIC0, 1);
    ret2 = fmc_flash_nor_clock_switch(FLASH_NOR_IDX_SPIC0, 160, &actual_mhz);
    ret3 = flash_nor_try_high_speed_mode(0, FLASH_NOR_DTR_4_BIT_MODE);

    DBG_DIRECT("ret0 %d , ret1 %d , ret2 %d , ret3 %d actual_mhz %d", ret0, ret1, ret2, ret3,
               actual_mhz);
    fmc_psram_clock_switch(PSRAM_IDX_SPIC1, 160, &actual_mhz);

    psram_winbond_opi_init();


    pm_display_freq_set(CLK_PLL1_SRC, 100, 100);
}
void lvgl_components_init(void)
{
#if (RTK_HAL_GPIO == 1)
    hw_pin_init();
#endif

#if (RTK_HAL_UART == 1)
    hw_uart_init();
#endif

#if (RTK_HAL_I2C == 1)
    hw_i2c_init();
#endif

#if (RTK_HAL_ADC == 1)
    hw_adc_init();
#endif

#if (RTK_HAL_LCD == 1)
    hw_lcd_init();
#endif

#if (RTK_HAL_TOUCH == 1)
    drv_touch_init();
#endif

#ifdef MODULE_VG_LITE
    hw_gpu_init();
#endif

#if (RTK_HAL_RTC == 1)
    hw_rtc_init();
#endif
#if (WS_USING_LETTER_SHELL == 1)
    userShellInit();
#endif
#ifdef RTK_PRIVATE_SERVICE

#endif

#if (DLPS_EN == 1)
    pwr_mgr_init();
#endif

}



/*-----------------------------------------------------------*/
