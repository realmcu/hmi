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
#include "app_key_button.h"
#endif

static gui_touch_port_data_t raw_data = {0};

static gui_wheel_port_data_t wheel_port_data = {0};

bool home_state = false;
bool back_state = false;
bool menu_state = false;
bool power_state = false;
uint32_t home_timestamp_ms_press = 0;
uint32_t home_timestamp_ms_release = 0;
uint32_t home_timestamp_ms_pressing = 0;
uint32_t back_timestamp_ms_press = 0;
uint32_t back_timestamp_ms_release = 0;
uint32_t back_timestamp_ms_pressing = 0;
uint32_t menu_timestamp_ms_press = 0;
uint32_t menu_timestamp_ms_release = 0;
uint32_t menu_timestamp_ms_pressing = 0;
uint32_t power_timestamp_ms_press = 0;
uint32_t power_timestamp_ms_release = 0;
uint32_t power_timestamp_ms_pressing = 0;

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

/***wheel device***/
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
/*(KEY1, KEY2, KEY3) connected to ADC_2, P2_1, and P3_5*/
void gui_port_indev_init(void)
{
    // gpio_button_init();

    gui_kb_create("Home", &home_state,
                  &home_timestamp_ms_press,
                  &home_timestamp_ms_release);
    gui_kb_create("Back", &back_state,
                  &back_timestamp_ms_press,
                  &back_timestamp_ms_release);
    gui_kb_create("Menu", &menu_state,
                  &menu_timestamp_ms_press,
                  &menu_timestamp_ms_release);
    gui_kb_create("Power", &power_state,
                  &power_timestamp_ms_press,
                  &power_timestamp_ms_release);

    app_key_button_init();
    DBG_DIRECT("func: %s line = %d!", __FUNCTION__, __LINE__);
    gui_indev_info_register(&indev);
}

