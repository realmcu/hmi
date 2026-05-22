/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _BOARD_H_
#define _BOARD_H_
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEY_RELEASE             0
#define KEY_PRESS               1
#define MAX_KEY_NUM             8
#define HYBRID_KEY_NUM          8
#define PIN_KEY0                0xFF
#define PIN_KEY1                ADC_2
#define PIN_KEY2                0xFF // key2 must be p2_1 due to hardware design, but this pin is configured by lcd BL, so key2 is not worked unless changing hardware design
#define PIN_KEY3                P3_5
#define PIN_KEY4                0xFF
#define PIN_KEY5                0xFF
#define PIN_KEY6                0xFF
#define PIN_KEY7                0xFF
#define RX_GDMA_BUFFER_SIZE             256
#define AW87390_I2C                  I2C1
#define AW87390_SCL                  ADC_0
#define AW87390_SDA                  ADC_1
#define DATA_UART_TX_PINMUX          P3_1
#define DATA_UART_RX_PINMUX          P3_0
#define SMART_PA_POWER_EN           P1_1
#define I2C_0_DAT_PINMUX            P0_1
#define I2C_0_CLK_PINMUX            P0_0
#define PIN_DSP_LOG_OUTPUT          P0_0
#if (TARGET_LCD_DEVICE == LCD_DEVICE_ST7265_RGB)
#define LCDC_DATA23         P9_6
#define LCDC_DATA22         P9_5
#define LCDC_DATA21         P9_4
#define LCDC_DATA20         P9_3
#define LCDC_DATA19         P9_2
#define LCDC_DATA18         P9_1
#define LCDC_DATA17         P8_7
#define LCDC_DATA16         P8_6
#define LCDC_DATA15         P8_5
#define LCDC_DATA14         P8_4
#define LCDC_DATA13         P8_3
#define LCDC_DATA12         P8_2
#define LCDC_DATA11         P8_1
#define LCDC_DATA10         P8_0
#define LCDC_DATA9          P4_7
#define LCDC_DATA8          P4_6
#define LCDC_DATA7          P4_5
#define LCDC_DATA6          P4_4
#define LCDC_DATA5          P4_3
#define LCDC_DATA4          P4_2
#define LCDC_DATA3          P4_1
#define LCDC_DATA2          P4_0
#define LCDC_DATA1          P2_7
#define LCDC_DATA0          P2_6
#define LCDC_RGB_WRCLK      P2_5
#define LCDC_HSYNC          P2_4
#define LCDC_CSN_DE         P2_3
#define LCDC_VSYNC          P2_2
//#define GPD0                P2_5
//#define GPD1                P2_7
//#define GPD2                P9_0
//#define GPD3                P2_6
#define LCDC_RESET          P9_0
#endif // TARGET_LCD_DEVICE == LCD_DEVICE_ST7265_RGB
#if F_APP_WIFI_SPI_MAP_SUPPORT
#if  USE_LCD_DEVICE_A0500_RGB
#define PIN_SPI_SCK                 P5_5//P3_5// P3_2  A3 clk
#define PIN_SPI_MOSI                P5_2// P3_3  A19 mosi
#define PIN_SPI_MISO                P5_3// P1_0  A20  miso
#define PIN_SPI_CS                  P5_4//P3_4// P1_1  A2 cs
#else
#define PIN_SPI_SCK                 P3_5// P3_2  A3 clk
#define PIN_SPI_MOSI                P5_2// P3_3  A19 mosi
#define PIN_SPI_MISO                P5_3// P1_0  A20  miso
#define PIN_SPI_CS                  P3_4// P1_1  A2 cs
#endif
#if USE_LCD_DEVICE_A0500_RGB
#define UART_TX_PIN        P3_2//P5_4// P3_2
#define UART_RX_PIN        P1_0//P5_5// P1_0
#else
#define UART_TX_PIN        P5_4// P3_2
#define UART_RX_PIN        P5_5// P1_0
#endif
#endif //F_APP_WIFI_SPI_MAP_SUPPORT
#ifdef __cplusplus
}
#endif
#endif /* _BOARD_H_ */
