/**
*********************24, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file      display_init.c
   * @brief     Source file for display project, mainly used for initialize modules
   * @author    Luke
   * @date      2024-08-29
   * @version   v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2022 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */

/*============================================================================*
 *                              Header Files
 *============================================================================*/

#include <stdlib.h>
#include "board.h"
#include "dlps.h"
#include "rtl876x.h"
#include "rtl876x_lib_platform.h"
#include "flash_device.h"
#include "os_sched.h"
#include "trace.h"
#include "display_init.h"
#if (RTK_BT_TASK == 1)
#include "bt_task.h"
#endif
#ifdef ENABLE_HONEYGUI
#include "gui_components_init.h"
#include "gui_server.h"
#endif


void display_task_init(void)
{
#if (RTK_BT_TASK == 1) || (RTK_BLE_TASK == 1)
    extern void bt_task_init(void);
    // bt_task_init();
    DBG_DIRECT("display_task_init %d", __LINE__);
#endif
#ifdef ENABLE_HONEYGUI
    gui_components_init();
    DBG_DIRECT("display_task_init %d", __LINE__);
#endif
#ifdef ENABLE_LVGL_GUI
    extern void rt_lvgl_demo_init(void);
    rt_lvgl_demo_init();
    DBG_DIRECT("finish rt_lvgl_demo_init");
#endif
}

void display_system_clock_init(void)
{
    extern uint32_t random_seed_value;
    srand(random_seed_value);
    if (set_system_clock(SYSTEM_90MHZ) == 0)
    {
        DBG_DIRECT("Set system clock 90M Fail!");
    }
    if (flash_try_high_speed(FLASH_MODE_4BIT) == 0)
    {
        DBG_DIRECT("Flash change to 4 BIT Mode Fail!");
    }
}

void display_components_init(void)
{

#if (RTK_HAL_GPIO == 1)
    extern void hw_pin_init(void);
//    hw_pin_init();
    DBG_DIRECT("display_components_init %d", __LINE__);
#endif

#if (RTK_HAL_UART == 1)
    extern void hw_uart_init(void);
    hw_uart_init();
    DBG_DIRECT("display_components_init %d", __LINE__);
#endif

#if (RTK_HAL_I2C == 1)
    extern void hw_i2c_init(void);
//    hw_i2c_init();
    DBG_DIRECT("display_components_init %d", __LINE__);
#endif

#if (RTK_HAL_ADC == 1)
    extern void hw_adc_init(void);
    hw_adc_init();
    DBG_DIRECT("display_components_init %d", __LINE__);
#endif

#if (RTK_HAL_LCD == 1)
    extern void hw_lcd_init(void);
    hw_lcd_init();
    DBG_DIRECT("display_components_init %d", __LINE__);
#endif

#if (RTK_HAL_TOUCH == 1)
    extern void drv_touch_init(void);
    drv_touch_init();
    DBG_DIRECT("display_components_init %d", __LINE__);
#endif

#if (RTK_HAL_RTC == 1)
    extern void drv_rtc_init(void);
    drv_rtc_init();
    DBG_DIRECT("display_components_init %d", __LINE__);
#endif
#if (USING_LETTER_SHELL == 1)
    extern void userShellInit(void);
    userShellInit();
#endif
#ifdef RTK_HAL_DLPS
    extern void pwr_mgr_init(void);
//    pwr_mgr_init();
#endif
}



/*-----------------------------------------------------------*/
