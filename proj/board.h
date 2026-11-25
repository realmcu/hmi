/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************
*                 DLPS Module Config
*******************************************************/

/** @defgroup IO Driver Config
  * @note user must config it firstly!! Do not change macro names!!
  * @{
  */
/* if use user define dlps enter/dlps exit callback function */
#define USE_USER_DEFINE_DLPS_EXIT_CB    1
#define USE_USER_DEFINE_DLPS_ENTER_CB   1

/* if use any peripherals below, #define it 1 */
#define USE_ADC_DLPS         0
#define USE_ENHTIM_DLPS      0
#define USE_GPIOA_DLPS       1
#define USE_GPIOB_DLPS       1
#define USE_I2C0_DLPS        1
#define USE_I2C1_DLPS        1
#define USE_I2C2_DLPS        1
#define USE_I2C3_DLPS        1
#define USE_IR_DLPS          0
#define USE_KEYSCAN_DLPS     0
#define USE_SPI0_DLPS        0
#define USE_SPI1_DLPS        0
#define USE_SPI0_SLAVE_DLPS  0
#define USE_TIM_DLPS         1
#define USE_UART0_DLPS       0
#define USE_UART1_DLPS       0
#define USE_UART2_DLPS       0
#define USE_UART3_DLPS       0
#define USE_UART4_DLPS       0
#define USE_UART5_DLPS       0

/* do not modify USE_IO_DRIVER_DLPS macro */
#define USE_IO_DRIVER_DLPS  (USE_ADC_DLPS | USE_ENHTIM_DLPS | USE_GPIOA_DLPS | USE_GPIOB_DLPS \
                             | USE_I2C0_DLPS | USE_I2C1_DLPS | USE_I2C2_DLPS | USE_I2C3_DLPS \
                             | USE_IR_DLPS | USE_KEYSCAN_DLPS | USE_TIM_DLPS | USE_SPI0_DLPS \
                             | USE_SPI1_DLPS | USE_SPI0_SLAVE_DLPS | USE_UART0_DLPS | USE_UART1_DLPS \
                             | USE_UART2_DLPS | USE_UART3_DLPS | USE_UART4_DLPS | USE_UART5_DLPS \
                             | USE_USER_DEFINE_DLPS_ENTER_CB \
                             | USE_USER_DEFINE_DLPS_EXIT_CB)
/*******************************************************/



#if (RTK_HAL_DLPS == 1)
#define DLPS_EN 0   // DLPS_ENABLE: 1(ENABLE) or 0(DISABLE)
#else
#define DLPS_EN 0   // WITHOUT HAL SET DLPS DISABLE
#endif


/*0: use FreeRTOS API, 1: USE OS Interface*/
#define  USE_OSIF                  1
#define  TEST_FTL                  0

/* 1 = the board has PSRAM on SPIC1 and it must be initialised in
 * display_system_clock_init(). Keep 0 until the LVGL draw buffers are moved
 * into the PSRAM section of mdk/app.sct. */
#define  FEATURE_PSRAM             0

#define  TEST_UART         1
#define  TEST_HW_TIMER     2
#define  TEST_LPC          3
#define  TEST_GPIO         4

#define  TEST_INTERRUPT             0 //TEST_UART //TEST_HW_TIMER // TEST_LPC // TEST_GPIO

#ifdef __cplusplus
}
#endif


/** @} */ /* End of group MEM_CONFIG */

#endif
