/*
 * Copyright(c) 2025, Realtek Semiconductor Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __TOUCH_CST816D_RD_H__
#define __TOUCH_CST816D_RD_H__


#include "rtl876x_gpio.h"
#include "rtl876x_rcc.h"
#include "rtl876x_tim.h"
#include "rtl876x_nvic.h"
#include "rtl876x_pinmux.h"
#include "rtl876x.h"
//#include "app_flags.h"

// #define TOUCH_I2C_SCL                             P8_5
// #define TOUCH_I2C_SDA                             P8_6

#define TOUCH_I2C_SCL                             P4_4
#define TOUCH_I2C_SDA                             P4_3

#define TOUCH_I2C_BUS                             I2C2
#define TOUCH_I2C_FUNC_SCL                        I2C2_CLK
#define TOUCH_I2C_FUNC_SDA                        I2C2_DAT
#define TOUCH_I2C_APBPeriph                       APBPeriph_I2C2
#define TOUCH_I2C_APBClock                        APBPeriph_I2C2_CLOCK
#define TOUCH_SLAVE_ADDRESS                       0x15

// #define TOUCH_INT_APBPeriph                       APBPeriph_GPIOB
// #define TOUCH_INT_APBPeriph_CLK                   APBPeriph_GPIOB_CLOCK
// #define TOUCH_INT_GROUP                           GPIOB
// #define TOUCH_INT                                 P8_7
// #define TOUCH_INT_HANDLER                         GPIOA2_Handler
// #define TOUCH_INT_IRQ                             GPIO48_IRQn
// #define TOUCH_INT_VECTORn                         GPIOB20_VECTORn

#define TOUCH_INT_APBPeriph                       APBPeriph_GPIOA
#define TOUCH_INT_APBPeriph_CLK                   APBPeriph_GPIOA_CLOCK
#define TOUCH_INT_GROUP                           GPIOA
#define TOUCH_INT                                 P0_0
#define TOUCH_INT_HANDLER                         GPIOA2_Handler
#define TOUCH_INT_IRQ                             GPIO0_IRQn
#define TOUCH_INT_VECTORn                         GPIO_A0_VECTORn


// #define TOUCH_RST                                 P8_4
#define TOUCH_RST                                 P0_3


typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t x_start;
    uint16_t y_start;
    uint32_t timestamp_ms_start;
    uint32_t timestamp_ms_pressing;
    uint16_t count_pressing;
    bool is_press;
} TOUCH_DATA;

void touch_driver_init(void);
bool rtk_touch_hal_read_all(uint16_t *x, uint16_t *y, bool *pressing);
void touch_gesture_enter_dlps(void);
void touch_gesture_exit_dlps(void);
TOUCH_DATA get_raw_touch_data(void);
bool touch_set_timeout_ms(uint32_t time);
void rtk_touch_hal_set_indicate(void (*indicate)(void *));
void rtk_touch_hal_int_config(bool enable);

#endif // MODULE_TOUCH_CST816T_POLLING_H
