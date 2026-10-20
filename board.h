/**
*********************************************************************************************************
*               Copyright(c) 2015, Realtek Semiconductor Corporation. All rights reserved.
*********************************************************************************************************
* @file      board.h
* @brief
* @details
* @author
* @date
* @version
* *********************************************************************************************************
*/


#ifndef _BOARD_H_
#define _BOARD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "rtl876x_pinmux.h"

#define CONFIG_DLPS_EN                          1
#define FEATURE_GSENSOR                         1
#define FEATURE_DISPLAY                         1
#define FEATURE_INTERACTION                     1
#define FEATURE_BATTERY_DETECT                  1
#define FEATURE_MOTOR                           1
#define FEATURE_CHARGER                         1
#define FEATURE_INPUT                           1
#define FEATURE_HEART_RATE_SENSOR               1
#define FEATURE_GSENSOR_ALGORITHM               1
#define FEATURE_AIRSYNC                         0
#define FEATURE_HID                             0
#define FEATURE_VOICE                           0
#define SUPPORT_SILENT_OTA                      1
#define TOUCH_GESTURE                           1
#define FEATURE_8763BF                          1
/*to be merge with button interaction*/
#define SERVICE_NUM                             4
#define APP_PRIVACY_EN                          1
#define FEATURE_PSRAM                           0

#define BLE_AUTO_TEST                           0
/*============================================================================*
 *                              OTA Config
 *============================================================================*/
#define SUPPORT_NORMAL_OTA                          0
#define ENABLE_AUTO_BANK_SWITCH                     0 //for qc test
#define ENABLE_BANK_SWITCH_COPY_APP_DATA            1
#define ENABLE_SLAVE_REQUEST_UPDATE_COON_PARA       0
#define F_BT_LE_5_0_SET_PHY_SUPPORT                 1
#define SUPPORT_OTA_PROTOCOL_TYPE_CHARACTERISTIC    1

extern uint8_t g_ota_mode;
#define DFU_TEMP_BUFFER_SIZE                        2048
#define DFU_BUFFER_CHECK_ENABLE                     0x1//(g_ota_mode & 0x1)


#if (ENABLE_SLAVE_REQUEST_UPDATE_COON_PARA ==1)
/*interval 1.25ms/step,    supervision_timeout 10ms/step*/
#define CONNECT_INTERVAL_MIN                        0x6  /* (7.5ms) */
#define CONNECT_INTERVAL_MAX                        0x6  /* (7.5ms) */
#define CONNECT_LATENCY                             0x0
#define SUPERVISION_TIMEOUT                         1000 /* (10s) */
#endif
/*============================================================================*/


/* if use user define dlps enter/dlps exit callback function */
#define USE_USER_DEFINE_DLPS_EXIT_CB      1
#define USE_USER_DEFINE_DLPS_ENTER_CB     1

/* if use any peripherals below, #define it 1 */

#define USE_I2C0_DLPS               0
#define USE_I2C1_DLPS               1
#define USE_TIM_DLPS                1
#define USE_QDECODER_DLPS           0
#define USE_IR_DLPS                 0
#define USE_RTC_DLPS                1
#define USE_ADC_DLPS                1
#define USE_SPI0_DLPS               0
#define USE_SPI1_DLPS               1
#define USE_SPI2W_DLPS              0
#define USE_KEYSCAN_DLPS            0
#define USE_DMIC_DLPS               0
#define USE_GPIO_DLPS               1
#define USE_PWM0_DLPS               0
#define USE_PWM1_DLPS               0
#define USE_PWM2_DLPS               1
#define USE_PWM3_DLPS               0
#define USE_IF8080_DLPS             1
#define USE_UART0_DLPS              1
#define USE_UART1_DLPS              1
#define USE_LOG_UART2_DLPS          0

#define USE_GDMACHANNEL0_DLPS       0
#define USE_GDMACHANNEL1_DLPS       0
#define USE_GDMACHANNEL2_DLPS       0
#define USE_GDMACHANNEL3_DLPS       0
#define USE_GDMACHANNEL4_DLPS       0
#define USE_GDMACHANNEL5_DLPS       0
#define USE_GDMACHANNEL6_DLPS       0
#define USE_GDMA_DLPS               (USE_GDMACHANNEL0_DLPS | USE_GDMACHANNEL1_DLPS | USE_GDMACHANNEL2_DLPS\
                                     | USE_GDMACHANNEL3_DLPS | USE_GDMACHANNEL4_DLPS | USE_GDMACHANNEL5_DLPS\
                                     | USE_GDMACHANNEL6_DLPS)

/* do not modify USE_IO_DRIVER_DLPS macro */
#define USE_IO_DRIVER_DLPS          (USE_ADC_DLPS | USE_DMIC_DLPS | USE_GPIO_DLPS\
                                     | USE_I2C0_DLPS | USE_I2C1_DLPS | USE_IR_DLPS\
                                     | USE_KEYSCAN_DLPS| USE_PWM0_DLPS | USE_PWM1_DLPS\
                                     | USE_PWM2_DLPS | USE_PWM3_DLPS | USE_QDECODER_DLPS\
                                     | USE_RTC_DLPS | USE_SPI0_DLPS | USE_SPI1_DLPS\
                                     | USE_SPI2W_DLPS | USE_TIM_DLPS | USE_UART0_DLPS\
                                     | USE_UART1_DLPS | USE_LOG_UART2_DLPS | USE_GDMA_DLPS\
                                     |USE_USER_DEFINE_DLPS_ENTER_CB\
                                     |USE_USER_DEFINE_DLPS_EXIT_CB\
                                     |USE_LCD_DLPS)

#if FEATURE_VOICE
//todo
#endif
#if FEATURE_GSENSOR
#define GSENSOR_I2C_SCL                                 H_2
#define GSENSOR_I2C_SDA                                 H_1
#define GSENSOR_I2C_BUS                                 I2C1
#define GSENSOR_I2C_FUNC_SCL                            I2C1_CLK
#define GSENSOR_I2C_FUNC_SDA                            I2C1_DAT
#define GSENSOR_I2C_APBPeriph                           APBPeriph_I2C1
#define GSENSOR_I2C_APBClock                            APBPeriph_I2C1_CLOCK
#define GSENSOR_SLAVE_ADDRESS                           (0x0F)
#endif

#if FEATURE_HEART_RATE_SENSOR
#define HEART_RATE_SENSOR_IS_VC31                     0 //for VC31
#define HEART_RATE_SENSOR_IS_VCHR02                 0   //for VC13B VC31S VC51B VC51S
#define HEART_RATE_SENSOR_IS_VCHR11                 1   //for LC11

#define HRS_I2C_SCL                                 H_2
#define HRS_I2C_SDA                                 H_1
#define HRS_I2C_BUS                                 I2C1
#define HRS_I2C_FUNC_SCL                            I2C1_CLK
#define HRS_I2C_FUNC_SDA                            I2C1_DAT
#define HRS_I2C_APBPeriph                           APBPeriph_I2C1
#define HRS_I2C_APBClock                            APBPeriph_I2C1_CLOCK
#define HRS_SLAVE_ADDRESS                           0x33

#define HRS_INT                                     P2_6
#define HRS_INT_IRQ                                 GPIO22_IRQn
#define HRS_INT_HANDLER                             GPIO22_Handler

#endif

#if FEATURE_DISPLAY
#define IF8080_PINGROUP_1       0
#if IF8080_PINGROUP_1
#define LCD_8080_RST                     P2_0
#define LCD_8080_BL                      P3_6 //P1_5 CK NO THIS PIN
/*because most 8080 lcd pins ara fixed*/
#define LCD_8080_D0                      P3_5
#define LCD_8080_D1                      P0_1
#define LCD_8080_D2                      P0_2
#define LCD_8080_D3                      P0_4
#define LCD_8080_D4                      P4_0
#define LCD_8080_D5                      P4_1
#define LCD_8080_D6                      P4_2
#define LCD_8080_D7                      P4_3

#define LCD_8080_CS                      P3_3
#define LCD_8080_DCX                     P3_4
#define LCD_8080_RD                      P0_5
#define LCD_8080_WR                      P3_2
#define LCD_FRAME_SYNC                   P3_0

#define LCD_DMA_CHANNEL_NUM              2
#define LCD_DMA_CHANNEL_INDEX            GDMA_Channel2
#define LCD_DMA_CHANNEL_IRQ              GDMA0_Channel2_IRQn
#define BL_PWM_TIM                       TIM4
#define GPIO_FRAME_SYNC_IRQ              GPIO24_IRQn
#define GPIO_FRAME_SYNC_HANDLER          GPIO24_Handler
#else
#define LCD_8080_RST                     P2_0
#define LCD_8080_BL                      P1_2 //need R15
/*because most 8080 lcd pins ara fixed*/
#define LCD_8080_D0                      P0_4
#define LCD_8080_D1                      P0_5
#define LCD_8080_D2                      P0_6
#define LCD_8080_D3                      P0_7
#define LCD_8080_D4                      P4_0
#define LCD_8080_D5                      P4_1
#define LCD_8080_D6                      P4_2
#define LCD_8080_D7                      P4_3

#define LCD_8080_CS                      P0_0
#define LCD_8080_DCX                     P1_5
#define LCD_8080_RD                      P1_6
#define LCD_8080_WR                      P0_2
#define LCD_FRAME_SYNC                   P0_1

#define LCD_DMA_CHANNEL_NUM              1
#define LCD_DMA_CHANNEL_INDEX            GDMA_Channel1
#define LCD_DMA_CHANNEL_IRQ              GDMA0_Channel1_IRQn
#define BL_PWM_TIM                       TIM4
#define GPIO_FRAME_SYNC_IRQ              GPIO1_IRQn
#define GPIO_FRAME_SYNC_HANDLER          GPIO1_Handler
#define QSPI_LCD_POWER                   P2_4
#endif
#endif

#if FEATURE_DISPLAY
#define LCD_SPI_CLK                                 P4_0
#define LCD_SPI_CS                                  P4_3
#define LCD_SPI_MOSI                                P4_2
#define LCD_SPI_BL                                  P2_7
#define LCD_SPI_DC                                  P4_1
#define LCD_SPI_RST                                 P2_0
#define LCD_SPI_RST_PIN                             GPIO_GetPin(LCD_SPI_RST)
#define LCD_SPI_DC_PIN                              GPIO_GetPin(LCD_SPI_DC)
#define LCD_SPI_BL_PIN                              GPIO_GetPin(LCD_SPI_BL)
#define LCD_SPI_BUS                                 SPI0
#define LCD_SPI_FUNC_CLK                            SPI0_CLK_MASTER
#define LCD_SPI_FUNC_MOSI                           SPI0_MO_MASTER
#define LCD_SPI_FUNC_CS                             SPI0_SS_N_0_MASTER
#define LCD_SPI_APBPeriph                           APBPeriph_SPI0
#define LCD_SPI_APBClock                            APBPeriph_SPI0_CLOCK
#define LCD_SPI_DMA_TX_HANDSHAKE                    GDMA_Handshake_SPI0_TX
#endif

#if FEATURE_BATTERY_DETECT
#define BATTERY_CHECK                               P2_1
#define BATTERY_CHECK_INDEX                         1
#endif

#if FEATURE_MOTOR
#define PWM_OUT                                     P0_0
#endif

#if FEATURE_CHARGER
#define CHARGER_INJECT_PIN                          P2_2
#define CHARGER_STATUS_PIN                          P1_5
#define CHARGER_DEBOUNCE_TIMER_ID                   1
#define USB_DEBOUNCE_TIMTER_ID                      2
#define CHARGER_DEBOUNCE_TIMER_INTERVAL             1000  //debonce timer
#define CHARGER_USB_DEBOUNCE_TIMTER_INTERVAL        1000  //
#define USB_CHECK_IRQ                               GPIO18_IRQn
#define UsbCheckHandler                             GPIO18_Handler
#define CHARGER_IRQ                                 GPIO13_IRQn
#define ChargerIntrHandler                          GPIO13_Handler
#endif

#if FEATURE_INPUT
#define GPIO_BUTTON_KEY                             P2_4
#define GPIO_BUTTON_HANDLER                         GPIO20_Handler
#define GPIO_BUTTON_IRQ                             GPIO20_IRQn
#endif

#if TOUCH_GESTURE
#define TOUCH_I2C_SCL                                   H_0
#define TOUCH_I2C_SDA                                   P2_2
#define TOUCH_I2C_BUS                                   I2C1
#define TOUCH_I2C_FUNC_SCL                              I2C1_CLK
#define TOUCH_I2C_FUNC_SDA                              I2C1_DAT
#define TOUCH_I2C_APBPeriph                             APBPeriph_I2C1
#define TOUCH_I2C_APBClock                              APBPeriph_I2C1_CLOCK
#define TOUCH_SLAVE_ADDRESS                             (0x15)
#define TOUCH_INT                                       H_1
#define TOUCH_INT_IRQ                                   GPIO26_IRQn
#define TOUCH_GESTURE_HANDLER                           GPIO26_Handler
#define TOUCH_RST                                       P2_3
#endif


#if FEATURE_8763BF
#define BBPRO_UART_BUS                                           UART1
#define BBPRO_HCI_TX                                             H_2
#define BBPRO_HCI_RX                                             P2_5
#define BBPRO_RST_PIN                                            P1_7
#define BBPRO_UART_VECTORn                                           Uart1_VECTORn
#define BBPRO_UART_APBPeriph                                     APBPeriph_UART1
#define BBPRO_UART_APBClock                                      APBPeriph_UART1_CLOCK
#define BBPRO_UART_FUNC_RX                                       UART1_RX
#define BBPRO_UART_FUNC_TX                                       UART1_TX
#define BBPRO_UART_IRQ                                           UART1_IRQn
#endif

#define DEBUG_UART                                               UART0
#define UART_DEBUG_TX                                            P3_1
#define UART_DEBUG_RX                                            P3_0
//we also need change irq cnad clock here

#if FEATURE_VOICE
/* voice encode type */
#define SW_MSBC_ENC                         1  /* software msbc encode */
#define SW_SBC_ENC                          2  /* software sbc encode */
#define SW_IMA_ADPCM_ENC                    3  /* software IMA/DVI adpcm encode */
#define SW_OPT_ADPCM_ENC                    4  /* software optimized adpcm encode */
#define PDM_CLK_PIN                         P2_7
#define PDM_DAT_PIN                         P2_4
#define AMIC_P                              P2_7
#define AMIC_N                              P2_6
#define AMIC_MICBIAS                        H_0
#define OUTPUT_FROM_UART                    0
#define DEBUG_VOICE_RX                      P3_1
#define DEBUG_VOICE_TX                      P3_0
#define VOICE_ENC_TYPE                      SW_SBC_ENC
//#define VOICE_ENC_TYPE                    SW_MSBC_ENC
#endif


/* here description pin assign for 8762CK 4bit mode in EVB */
#if 0
#define P0_0    MOTOR_PWM
#define P0_1    LCD_D1        /* for wristband PWM, because it is LED/motor */
#define P0_2    LCD_D2
#define P0_3    LOG_USE
#define P0_4    LCD_D3          /* for wristband lcd scl */

#define P1_0    SWDIO_USE(also button no confict)
#define P1_1    SWDCLK_USE(TOUCH_INT)
#define P1_2    FOR_PSRAM_SWITCH
#define P1_3    PLATFORM_4_BIT        /* for platform 4bit mode */
#define P1_4    PLATFORM_4_BIT        /* for platform 4bit mode */

#define P2_0    LCD_RST(also lcd read pin)
#define P2_1    ADC_BATTERY
#define P2_2    CHARGE_INJECT  /* for wristband i2c interrupt */
#define P2_3    TOUCH_RST
#define P2_4    NO_USE
#define P2_5    NO_USE
#define P2_6    BBPRO_TX
#define P2_7    BBPRO_RX

#define P3_0    UART_TX_USE(also lcd te sync)
#define P3_1    UART_RX_USE
#define P3_2    LCD_WR
#define P3_3    LCD_CS
#define P3_4    LCD_DC
#define P3_5    LCD_D0        /* for wristband button */
#define P3_6    LCD_BL          /* for wristband lcd bl */

#define P4_0    LCD_D4          /* for wristband lcd D4 */
#define P4_1    LCD_D5          /* for wristband lcd D5 */
#define P4_2    LCD_D6          /* for wristband lcd D6 */
#define P4_3    LCD_D7          /* for wristband lcd D7 */
#endif


#define MP_TEST_MODE_SUPPORT_HCI_UART_TEST      1  /* set 1 to support HCI Uart Test Mode */
#define MP_TEST_MODE_SUPPORT_DATA_UART_TEST     1  /* set 1 to support Data Uart Test Mode */
#define MP_TEST_MODE_SUPPORT_SINGLE_TONE_TEST   1  /* set 1 to support SingleTone Test Mode */
#define MP_TEST_MODE_SUPPORT_FAST_PAIR_TEST     1  /* set 1 to support Fast Pair Test */
#define MP_TEST_MODE_SUPPORT_AUTO_K_RF          1  /* set 1 to support Auto K RF */

#define MP_TEST_UART_TX_PIN     P3_0
#define MP_TEST_UART_RX_PIN     P3_1

#define MP_TEST_MODE_TRIG_PIN   P2_2

#ifdef __cplusplus
}
#endif

#endif  /* _BOARD_H_ */

