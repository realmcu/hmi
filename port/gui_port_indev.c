/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "guidef.h"
#include "gui_api.h"
#include "gui_port.h"
#include "kb_algo.h"
#include "gpio_api.h"
#include "gpio_irq_api.h"

gui_touch_port_data_t tp_port_data = {0};

gui_wheel_port_data_t wheel_port_data = {0};


gui_touch_port_data_t *port_touchpad_get_data(void)
{
    tp_port_data.timestamp_ms = gui_ms_get();//todo
    return &tp_port_data;
}

gui_wheel_port_data_t *port_wheel_get_data(void)
{
    wheel_port_data.timestamp_ms = gui_ms_get();
    return &wheel_port_data;
}


static struct gui_indev indev =
{
    .tp_get_data = port_touchpad_get_data,
    .wheel_get_port_data = port_wheel_get_data,
    .tp_height = 0,
    .tp_witdh = 0,
    .touch_timeout_ms = 110,
    .long_button_time_ms = 800,
    .short_button_time_ms = 800,
    .quick_slide_time_ms = 200,

    .kb_short_button_time_ms = 30,
    .kb_long_button_time_ms = 800,
};




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

#define HOME_BUTTON_PIN  PB_17
#define BACK_BUTTON_PIN  PB_14
#define MENU_BUTTON_PIN  PB_16
#define POWER_BUTTON_PIN  PB_15

gpio_irq_t home_button_irq;
gpio_irq_t back_button_irq;
gpio_irq_t menu_button_irq;
gpio_irq_t power_button_irq;

void home_button_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;
    gpio_irq_disable(&home_button_irq);
    uint32_t level = GPIO_ReadDataBit(home_button_irq.pin);
    if(level == 0)
    {
        gpio_irq_set(&home_button_irq, IRQ_RISE, 1);
        home_timestamp_ms_press = gui_ms_get();
        home_state = true;
    }
    else
    {
        gpio_irq_set(&home_button_irq, IRQ_FALL, 1);
        home_timestamp_ms_release = gui_ms_get();
        home_state = false;
    }
    // TODO: handle button press event here

    gpio_irq_enable(&home_button_irq);
}

void back_button_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;
    gpio_irq_disable(&back_button_irq);
    uint32_t level = GPIO_ReadDataBit(back_button_irq.pin);
    if(level == 0)
    {
        gpio_irq_set(&back_button_irq, IRQ_RISE, 1);
        back_timestamp_ms_press = gui_ms_get();
        back_state = true;
    }
    else
    {
        gpio_irq_set(&back_button_irq, IRQ_FALL, 1);
        back_timestamp_ms_release = gui_ms_get();
        back_state = false;
    }

    gpio_irq_enable(&back_button_irq);
}

void menu_button_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;
    gpio_irq_disable(&menu_button_irq);
    uint32_t level = GPIO_ReadDataBit(menu_button_irq.pin);
    if(level == 0)
    {
        gpio_irq_set(&menu_button_irq, IRQ_RISE, 1);
        menu_timestamp_ms_press = gui_ms_get();
        menu_state = true;
    }
    else
    {
        gpio_irq_set(&menu_button_irq, IRQ_FALL, 1);
        menu_timestamp_ms_release = gui_ms_get();
        menu_state = false;
    }

    gpio_irq_enable(&menu_button_irq);
}

void power_button_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;
    gpio_irq_disable(&power_button_irq);
    uint32_t level = GPIO_ReadDataBit(power_button_irq.pin);
    if(level == 0)
    {
        gpio_irq_set(&power_button_irq, IRQ_RISE, 1);
        power_timestamp_ms_press = gui_ms_get();
        power_state = true;
    }
    else
    {
        gpio_irq_set(&power_button_irq, IRQ_FALL, 1);
        power_timestamp_ms_release = gui_ms_get();
        power_state = false;
    }

    gpio_irq_enable(&power_button_irq);
}

void button_init(void)
{
    gpio_irq_init(&home_button_irq, HOME_BUTTON_PIN, home_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&home_button_irq)));
    gpio_irq_pull_ctrl(&home_button_irq, PullUp);
    gpio_irq_set(&home_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&home_button_irq);

    gpio_irq_init(&back_button_irq, BACK_BUTTON_PIN, back_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&back_button_irq)));
    gpio_irq_pull_ctrl(&back_button_irq, PullUp);
    gpio_irq_set(&back_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&back_button_irq);

    gpio_irq_init(&power_button_irq, POWER_BUTTON_PIN, power_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&power_button_irq)));
    gpio_irq_pull_ctrl(&power_button_irq, PullUp);
    gpio_irq_set(&power_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&power_button_irq);

    gpio_irq_init(&menu_button_irq, MENU_BUTTON_PIN, menu_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&menu_button_irq)));
    gpio_irq_pull_ctrl(&menu_button_irq, PullUp);
    gpio_irq_set(&menu_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&menu_button_irq);


}

void gui_port_indev_init(void)
{
    gui_indev_info_register(&indev);
    button_init();
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
}




