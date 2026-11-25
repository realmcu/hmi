/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "rtl_tim.h"
#include "rtl_rcc.h"
#include "rtl_nvic.h"
#include "app_msg.h"
#include "app_task.h"
#include "trace.h"
#include <app_section.h>

/*============================================================================*
 *                              Macros
 *============================================================================*/
#define TIMER_NUM               TIM4
#define TIMER_IRQN              Timer4_IRQn
#define WDG_Timer_Handler       Timer4_Handler
#define TIMER_PERIOD            (2*1000*1000*40-1)  //2s for 40M clock

/*============================================================================*
 *                              Functions Declaration
 *============================================================================*/
static void watch_dog_timer_driver_init(void);

void WDG_Timer_Handler(void) DATA_RAM_FUNCTION;

/*============================================================================*
 *                              Local Functions
 *============================================================================*/
/******************************************************************
 * @brief  Initialize watch dog timer.
 * @param  none
 * @return none
 * @retval void
 */
void watch_dog_timer_driver_init(void)
{
    RCC_PeriphClockCmd(APBPeriph_TIMER, APBPeriph_TIMER_CLOCK, ENABLE);

    TIM_TimeBaseInitTypeDef TIM_InitStruct;
    TIM_StructInit(&TIM_InitStruct);

    TIM_InitStruct.TIM_PWM_En = DISABLE;
    TIM_InitStruct.TIM_Period = TIMER_PERIOD ;
    TIM_InitStruct.TIM_Mode = TIM_Mode_UserDefine;
    TIM_TimeBaseInit(TIMER_NUM, &TIM_InitStruct);

    /*  Enable TIMER IRQ  */
    NVIC_InitTypeDef NVIC_InitStruct;
    NVIC_InitStruct.NVIC_IRQChannel = TIMER_IRQN;
    NVIC_InitStruct.NVIC_IRQChannelPriority = 3;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    TIM_ClearINT(TIMER_NUM);
    TIM_INTConfig(TIMER_NUM, ENABLE);
    TIM_Cmd(TIMER_NUM, ENABLE);
}

/*============================================================================*
 *                              Global Functions
 *============================================================================*/
/******************************************************************
 * @brief  enable watch dog timer.
 * @param  none
 * @return none
 * @retval void
 */
void reset_watch_dog_timer_enable(void)
{
    watch_dog_timer_driver_init();
}

/******************************************************************
 * @brief  watch dog timer handle.
 * @param  none
 * @return none
 * @retval void
 */
void WDG_Timer_Handler(void)
{
    TIM_ClearINT(TIMER_NUM);
    TIM_Cmd(TIMER_NUM, DISABLE);
    //send message to app task in which reset the watch dog timer
    T_IO_MSG wdg_tmr_io_msg = {0};
    wdg_tmr_io_msg.type = IO_MSG_TYPE_RESET_WDG_TIMER;
    if (false == app_send_msg_to_apptask(&wdg_tmr_io_msg))
    {
        APP_PRINT_ERROR0("[WDG] send IO_MSG_TYPE_RESET_WDG_TIMER message failed!");
    }

    TIM_Cmd(TIMER_NUM, ENABLE);
}


