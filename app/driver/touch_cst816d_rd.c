/*
 * Copyright(c) 2025, Realtek Semiconductor Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "os_timer.h"
#include "app_msg.h"
//#include "hub_task.h"
#include "platform_utils.h"
#include "vector_table.h"
#include "rtl876x_i2c.h"
#include "trace.h"
//#include "app_dlps.h"
#include "os_sched.h"
#include "touch_cst816d_rd.h"
#include "stdio.h"

static void touch_gesture_release_timeout(void *pxTimer);
static void *touch_gesture_release_timer = NULL;
void touch_get_chip_id(uint8_t *p_chip_id);
static TOUCH_DATA cur_point = {0};
static uint32_t touch_timeout_ms = 30;

void rtk_touch_hal_set_indicate(void (*indicate)(void *))
{
    (void)indicate;
    return;
}
void rtk_touch_hal_int_config(bool enable)
{
    (void)enable;
    return;
}

static void touch_gesture_release_timeout(void *pxTimer)
{
//    cur_point.x = 0;
//    cur_point.y = 0;
//    cur_point.x_start = 0;
//    cur_point.y_start = 0;
//    cur_point.timestamp_ms_start = 0;
//    cur_point.timestamp_ms_pressing = 0;
    cur_point.count_pressing = 0;
    cur_point.is_press = 0;
}

/**
  * @brief  Initialize touch device
  * @param  None
  *
  * @retval None
  */
static void touch_device_init(void)
{
    const uint8_t pull_strength = 1;
    Pad_PullConfigValue(TOUCH_I2C_SCL, pull_strength);
    Pad_PullConfigValue(TOUCH_I2C_SDA, pull_strength);

    Pinmux_Config(TOUCH_I2C_SCL, TOUCH_I2C_FUNC_SCL);
    Pinmux_Config(TOUCH_I2C_SDA, TOUCH_I2C_FUNC_SDA);
    Pinmux_Config(TOUCH_INT, DWGPIO);

    Pad_Config(TOUCH_I2C_SCL, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(TOUCH_I2C_SDA, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(TOUCH_INT, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);

    /* Enable GPIO and hardware timer's clock */
    RCC_PeriphClockCmd(TOUCH_INT_APBPeriph,  TOUCH_INT_APBPeriph_CLK,  ENABLE);

    RamVectorTableUpdate(TOUCH_INT_VECTORn, TOUCH_INT_HANDLER);
    /* Initialize GPIO as interrupt mode */
    GPIO_InitTypeDef GPIO_Param;
    GPIO_StructInit(&GPIO_Param);
    GPIO_Param.GPIO_PinBit = GPIO_GetPin(TOUCH_INT);
    GPIO_Param.GPIO_Mode = GPIO_Mode_IN;
    GPIO_Param.GPIO_ITCmd = ENABLE;
    GPIO_Param.GPIO_ITTrigger = GPIO_INT_Trigger_EDGE;
    GPIO_Param.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_LOW; //GPIO_INT_POLARITY_ACTIVE_HIGH;
    GPIOx_Init(TOUCH_INT_GROUP, &GPIO_Param);

    NVIC_InitTypeDef NVIC_InitStruct;
    NVIC_InitStruct.NVIC_IRQChannel = TOUCH_INT_IRQ;
    NVIC_InitStruct.NVIC_IRQChannelPriority = 3;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    RCC_PeriphClockCmd(TOUCH_I2C_APBPeriph, TOUCH_I2C_APBClock, DISABLE);
    RCC_PeriphClockCmd(TOUCH_I2C_APBPeriph, TOUCH_I2C_APBClock, ENABLE);
    I2C_InitTypeDef  I2C_InitStructure;
    I2C_StructInit(&I2C_InitStructure);
    I2C_InitStructure.I2C_Clock = 40000000;
    I2C_InitStructure.I2C_ClockSpeed   = 400000;
    I2C_InitStructure.I2C_DeviveMode   = I2C_DeviveMode_Master;
    I2C_InitStructure.I2C_AddressMode  = I2C_AddressMode_7BIT;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_Init(TOUCH_I2C_BUS, &I2C_InitStructure);
    I2C_Cmd(TOUCH_I2C_BUS, ENABLE);

    GPIOx_MaskINTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), ENABLE);
    GPIOx_INTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), DISABLE);
}

void touch_gesture_exit_dlps(void)
{
//    touch_device_init();
    Pad_Config(TOUCH_I2C_SCL, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(TOUCH_I2C_SDA, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(TOUCH_INT, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
}

void touch_gesture_enter_dlps(void)
{
//    Pad_Config(TOUCH_RST, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(TOUCH_I2C_SCL, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(TOUCH_I2C_SDA, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(TOUCH_INT, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_DISABLE, PAD_OUT_LOW);

}

void touch_driver_init(void)
{
    DBG_DIRECT("touch_driver_init");
    touch_device_init();

    Pad_Config(TOUCH_RST, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
    platform_delay_ms(10);
    Pad_Config(TOUCH_RST, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
    platform_delay_ms(10);
    Pad_Config(TOUCH_RST, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
    platform_delay_ms(100);

    if (touch_gesture_release_timer == NULL)
    {
        os_timer_create(&touch_gesture_release_timer, "touch gesture release timer", 1, touch_timeout_ms,
                        false,
                        touch_gesture_release_timeout);
    }

    GPIOx_MaskINTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), ENABLE);
    GPIOx_INTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), ENABLE);
    GPIOx_ClearINTPendingBit(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT));
    GPIOx_MaskINTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), DISABLE);

    __enable_irq();

    uint8_t chip_id[4];
    touch_get_chip_id(chip_id);
//    DBG_DIRECT("touch_driver_init end");
}

void rtk_touch_hal_init(void)
{
    touch_driver_init();
}

void touch_write(uint8_t reg, uint8_t data)
{
    uint8_t I2C_WriteBuf[2] = {reg, data};
    I2C_SetSlaveAddress(TOUCH_I2C_BUS, TOUCH_SLAVE_ADDRESS);
    I2C_Status res = I2C_MasterWrite(TOUCH_I2C_BUS, I2C_WriteBuf, 2);
    if (res != I2C_Success)
    {
        APP_PRINT_INFO1("ERROR! touch_write I2C_MasterWrite: %d", res);
    }
}

void touch_read(uint8_t reg, uint8_t *p_data, uint8_t len)
{
    I2C_SetSlaveAddress(TOUCH_I2C_BUS, TOUCH_SLAVE_ADDRESS);
    I2C_Status res = I2C_MasterWrite(TOUCH_I2C_BUS, &reg, 1);
    if (res != I2C_Success)
    {
        APP_PRINT_INFO1("ERROR! touch_read I2C_MasterWrite: %d", res);
    }
    platform_delay_us(100);
    res = I2C_MasterRead(TOUCH_I2C_BUS, p_data, len);
    if (res != I2C_Success)
    {
        APP_PRINT_INFO1("ERROR! touch_read I2C_MasterRead: %d", res);
    }
}

void touch_read_32(uint32_t reg, uint8_t *p_data, uint8_t len)
{
    uint8_t reg_write[4] = {0};
    reg_write[0] = (uint8_t)(reg >> 24);
    reg_write[1] = (uint8_t)(reg >> 16);
    reg_write[2] = (uint8_t)(reg >> 8);
    reg_write[3] = (uint8_t)(reg);
    I2C_SetSlaveAddress(TOUCH_I2C_BUS, TOUCH_SLAVE_ADDRESS);
    I2C_Status res = I2C_MasterWrite(TOUCH_I2C_BUS, reg_write, 4);
    if (res != I2C_Success)
    {
        APP_PRINT_INFO1("ERROR! touch_read I2C_MasterWrite: %d", res);
    }
    platform_delay_us(1);
    res = I2C_MasterRead(TOUCH_I2C_BUS, p_data, len);
    if (res != I2C_Success)
    {
        APP_PRINT_INFO1("ERROR! touch_read I2C_MasterRead: %d", res);
    }
}

void touch_get_chip_id(uint8_t *p_chip_id)
{
    touch_read(0xa7, p_chip_id, 4);
    APP_PRINT_INFO1("[cst0x6 get chip id] -- %b", TRACE_BINARY(4, p_chip_id));
    DBG_DIRECT("[cst0x6 get chip id] -- 0x%x 0x%x 0x%x 0x%x ", p_chip_id[0], p_chip_id[1], p_chip_id[2],
               p_chip_id[3]);

    touch_read(0xa9, p_chip_id, 4);
    APP_PRINT_INFO1("[cst0x6 get chip version] -- %b", TRACE_BINARY(4, p_chip_id));
    DBG_DIRECT("[cst0x6 get chip version] -- 0x%x 0x%x 0x%x 0x%x ", p_chip_id[0], p_chip_id[1],
               p_chip_id[2], p_chip_id[3]);
}

union rpt_point_t
{
    struct
    {
        unsigned char x_l8;
        unsigned char y_l8;
        unsigned char z;
        unsigned char x_h4: 4;
        unsigned char y_h4: 4;
        unsigned char id: 4;
        unsigned char event: 4;
    } rp;
    unsigned char data[5];
};

bool rtk_touch_hal_read_all(uint16_t *x, uint16_t *y, bool *pressing)
{
    // DBG_DIRECT("%s %d", __FUNCTION__, __LINE__);
    uint8_t data[24] = {0};
//    touch_read(0x03, buf, 4);
    // touch_read_32(0, data, 1);
    touch_read(0, data, 24);

#if 0
    uint8_t point_num = data[1];
    union rpt_point_t *ppt;
    int16_t x_buff = 0;
    int16_t y_buff = 0;
    uint8_t event_id = 0;
    DBG_DIRECT("----11111----> point_num : 0x%X ", point_num);
    point_num = 1;
    if (point_num == 0)
    {
        return false;
    }
    ppt = (union rpt_point_t *)&data[2];
    if (point_num)
    {
        for (uint8_t i = 0; i < point_num; i ++)
        {
            event_id = ppt->rp.event;
            x_buff = (unsigned int)(ppt->rp.x_h4 << 8) | ppt->rp.x_l8;
            y_buff = (unsigned int)(ppt->rp.y_h4 << 8) | ppt->rp.y_l8;
        }
        // DBG_DIRECT("event_id = %d, x %d, y %d",event_id, x_buff, y_buff);
    }
    *x = x_buff;
    *y = y_buff;
#endif
    int16_t x_buff = 0;
    int16_t y_buff = 0;
    if (data[3] >> 6 == 2)
    {
        *pressing = true;
    }
    else
    {
        *pressing = false;
    }
    x_buff = (((data[5] & 0x0f) << 8) | data[6]);
    y_buff = (((data[3] & 0x0f) << 8) | data[4]);

    // *x = 360 - y_buff;
    // *y = 360 - x_buff;
    *x = y_buff;
    *y = x_buff;

    // DBG_DIRECT("x %d, y %d", *x, *y);
//    if (buf[0] >> 6 == 2)
//    {
//        *pressing = true;
//    }
//    else
//    {
//        *pressing = false;
//    }
//    *pressing = true;
//    *x = 454 - (((buf[0] & 0x0f) << 8) | buf[1]);
//    *y = 454 - (((buf[2] & 0x0f) << 8) | buf[3]);

    return true;
}

void TOUCH_INT_HANDLER(void)
{
    /*  Mask GPIO interrupt */
    // DBG_DIRECT("TOUCH_INT_HANDLER!!");
    GPIOx_INTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), DISABLE);
    GPIOx_MaskINTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), ENABLE);
    GPIOx_ClearINTPendingBit(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT));

    cur_point.count_pressing++;
    if (cur_point.count_pressing <= 1)
    {
        cur_point.timestamp_ms_start = os_sys_time_get();
        rtk_touch_hal_read_all(&cur_point.x_start, &cur_point.y_start, &cur_point.is_press);
        cur_point.x = cur_point.x_start;
        cur_point.y = cur_point.y_start;
        cur_point.timestamp_ms_pressing = cur_point.timestamp_ms_start;
    }
    else
    {
        cur_point.timestamp_ms_pressing = os_sys_time_get();
        rtk_touch_hal_read_all(&cur_point.x, &cur_point.y, &cur_point.is_press);
    }
    os_timer_restart(&touch_gesture_release_timer, touch_timeout_ms);

    GPIOx_INTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), ENABLE);
    GPIOx_MaskINTConfig(TOUCH_INT_GROUP, GPIO_GetPin(TOUCH_INT), DISABLE);
}

TOUCH_DATA get_raw_touch_data(void)
{
    return cur_point;
}

bool touch_set_timeout_ms(uint32_t time)
{
    if (time > 0)
    {
        touch_timeout_ms = time;
        if (touch_gesture_release_timer != NULL)
        {
            os_timer_delete(&touch_gesture_release_timer);
            touch_gesture_release_timer = NULL;
        }
        os_timer_create(&touch_gesture_release_timer, "touch gesture release timer", 1, touch_timeout_ms,
                        false, touch_gesture_release_timeout);
        return true;
    }
    return false;
}
