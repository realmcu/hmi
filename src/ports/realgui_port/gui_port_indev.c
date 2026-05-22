/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "app_gui.h"
#include "guidef.h"
#include "gui_port.h"
#include "os_sched.h"
#include "os_sync.h"
#include "wdg.h"
#include "kb_algo.h"
#include "wheel_algo.h"
#include "tp_algo.h"
#include "gui_api.h"
#include "trace.h"

#if (TARGET_LCD_DEVICE == LCD_DEVICE_SH8601Z_LCDC_QSPI)
#include "lcd_sh8601z_410_502_qspi.h"
#include "touch_CHSC6417.h"
#include "key_button_8773e.h"
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_ST7265_RGB)
#include "lcd_st7265_800480_rgb.h"
#include "drv_lcd.h"
//#include "module_button.h"
#endif

static gui_touch_port_data_t raw_data = {0};

static gui_wheel_port_data_t wheel_port_data = {0};

#if TARGET_TOUCH_DEVICE != TOUCH_DEVICE_INVALID
/***touch device***/
gui_touch_port_data_t *port_touchpad_get_data()
{
    TOUCH_DATA touch_raw_data;
    bool pressing = 0;
    /*get touch data*/
    uint32_t s = os_lock();
    touch_raw_data = get_raw_touch_data();
    os_unlock(s);

    raw_data.x_coordinate_start = touch_raw_data.x_start;
    raw_data.y_coordinate_start = touch_raw_data.y_start;
    raw_data.timestamp_ms_start = touch_raw_data.timestamp_ms_start;

    raw_data.x_coordinate = touch_raw_data.x;
    raw_data.y_coordinate = touch_raw_data.y;
    raw_data.timestamp_ms = touch_raw_data.timestamp_ms_pressing;

    raw_data.width = 0;
    pressing = touch_raw_data.is_press;

    //gui_log("x %d y %d time %d cnt %d press %d",raw_data.x_coordinate, raw_data.y_coordinate, raw_data.timestamp_ms, raw_data.count_pressing, pressing);
    if (pressing == true)
    {
        raw_data.event = GUI_TOUCH_EVENT_DOWN;
    }
    else
    {
        raw_data.event = GUI_TOUCH_EVENT_UP;
    }
    return &raw_data;
//    uint16_t x = 0;
//    uint16_t y = 0;
//    bool pressing = 0;

//    if (rtk_touch_hal_read_all(&x, &y, &pressing) == false)
//    {
//        return NULL;
//    }
//    if (pressing == true)
//    {
//        raw_data.event = 2;
//    }
//    else
//    {
//        raw_data.event = 1;
//    }


//    raw_data.timestamp_ms = os_sys_time_get();

//    raw_data.width = 0;
//    raw_data.x_coordinate = x;
//    raw_data.y_coordinate = y;
//    gui_log("event = %d, x = %d, y = %d, \n", raw_data.event, raw_data.x_coordinate, raw_data.y_coordinate);

//    return &raw_data;
}
#endif



gui_touch_port_data_t *port_touchpad_get_data()
{
    bool pressing = 0;
    /*get touch data*/

    raw_data.x_coordinate_start = 0;
    raw_data.y_coordinate_start = 0;
    raw_data.timestamp_ms_start = 0;

    raw_data.x_coordinate = 0;
    raw_data.y_coordinate = 0;
    raw_data.timestamp_ms = 0;

    raw_data.width = 0;

    raw_data.event = GUI_TOUCH_EVENT_UP;
    return &raw_data;
}




/***kb device***/
void port_button_set_indicate(void (*callback)(void))
{
    return;
}


/***wheel device***/
//todo
gui_wheel_port_data_t *port_wheel_get_data(void)
{
    return &wheel_port_data;
}

static struct gui_indev indev =
{
    .tp_get_data = port_touchpad_get_data,
    .wheel_get_port_data = port_wheel_get_data,

    .touch_timeout_ms = 30,
    .long_button_time_ms = 800,
    .short_button_time_ms = 300,
    .quick_slide_time_ms = 50,

    .kb_long_button_time_ms = 2000,
    .kb_short_button_time_ms = 60,

};

extern void gui_indev_info_register(struct gui_indev *info);
void gui_port_indev_init(void)
{
//    extern void touch_driver_init(void);
//    touch_driver_init();
//    gpio_button_init();
//    touch_set_timeout_ms(indev.touch_timeout_ms);
    DBG_DIRECT("func: %s line = %d!", __FUNCTION__, __LINE__);
    gui_indev_info_register(&indev);
}

