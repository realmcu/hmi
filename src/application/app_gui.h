/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_GUI_H_
#define _APP_GUI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LCD_INTERFACE_QSPI                   0
#define LCD_INTERFACE_8080                   1
#define LCD_INTERFACE_QSPI_1_BIT             2
#define LCD_INTERFACE_LCDC_QSPI              3
#define LCD_INTERFACE_SPI                    4
#define LCD_INTERFACE_LCDC_RGB               5

#define LCD_DEVICE_ST7789                    0
#define LCD_DEVICE_NT35110                   1
#define LCD_DEVICE_RM69330                   2
#define LCD_DEVICE_SH8601Z                   3
#define LCD_DEVICE_SH8601Z_QSPI_1_BIT        4
#define LCD_DEVICE_ST77916                   5
#define LCD_DEVICE_SH8601Z_LCDC_QSPI         6
#define LCD_DEVICE_ST7801                    7
#define LCD_DEVICE_SH8601Z_SPI               8
#define LCD_DEVICE_ST7265_RGB                9
#define LCD_DEVICE_INVALID                   10  //valid lcd device adds before this and update invalid index
#define LCD_DEVICE_LCDC_A0500                11

#define TOUCH_DEVICE_CS816T                  0
#define TOUCH_DEVICE_GT9147                  1
#define TOUCH_DEVICE_CST816D                 2
#define TOUCH_DEVICE_CHSC6417                3
#define TOUCH_DEVICE_INVALID                 4  //valid touch device adds before this and update invalid index

// Macro to use the desired LCD device
// Set to 1 to choose LCD_DEVICE_ST7265_RGB
#define USE_LCD_DEVICE_ST7265_RGB            1
// Set to 1 to choose LCD_DEVICE_LCDC_A0500
#define USE_LCD_DEVICE_A0500_RGB             0
// Set to 1 to choose LCD_DEVICE_SH8601Z_LCDC_QSPI
#define USE_LCD_DEVICE_SH8601Z_LCDC_QSPI     0

#if ((CONFIG_SOC_SERIES_RTL8773E == 1) || (CONFIG_SOC_SERIES_RTL8773D== 1) )&& (USE_LCD_DEVICE_ST7265_RGB == 1)
#define TARGET_LCD_DEVICE                    LCD_DEVICE_ST7265_RGB
#define TARGET_TOUCH_DEVICE                  TOUCH_DEVICE_INVALID
#elif ((CONFIG_SOC_SERIES_RTL8773E == 1) || (CONFIG_SOC_SERIES_RTL8773D== 1) ) && (USE_LCD_DEVICE_A0500_RGB == 1)
#define TARGET_LCD_DEVICE                    LCD_DEVICE_LCDC_A0500
#define TARGET_TOUCH_DEVICE                  TOUCH_DEVICE_INVALID
#elif ((CONFIG_SOC_SERIES_RTL8773E == 1) || (CONFIG_SOC_SERIES_RTL8773D== 1) ) && (USE_LCD_DEVICE_SH8601Z_LCDC_QSPI == 1)
#define TARGET_LCD_DEVICE                    LCD_DEVICE_SH8601Z_LCDC_QSPI
#define TARGET_TOUCH_DEVICE                  TOUCH_DEVICE_INVALID
#else
#define TARGET_LCD_DEVICE                    LCD_DEVICE_ST77916
#define TARGET_TOUCH_DEVICE                  TOUCH_DEVICE_CST816D
#endif

#if (TARGET_LCD_DEVICE == LCD_DEVICE_ST7789)
#define LCD_INTERFACE                        LCD_INTERFACE_QSPI
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_NT35110)
#define LCD_INTERFACE                        LCD_INTERFACE_8080
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_RM69330)
#define LCD_INTERFACE                        LCD_INTERFACE_QSPI
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_SH8601Z)
#define LCD_INTERFACE                        LCD_INTERFACE_QSPI
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_SH8601Z_QSPI_1_BIT)
#define LCD_INTERFACE                        LCD_INTERFACE_QSPI_1_BIT
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_ST77916)
#define LCD_INTERFACE                        LCD_INTERFACE_QSPI
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_SH8601Z_LCDC_QSPI)
#define LCD_INTERFACE                        LCD_INTERFACE_LCDC_QSPI
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_ST7801)
#if (CONFIG_SOC_SERIES_RTL8773E == 1)
#define LCD_INTERFACE                        LCD_INTERFACE_LCDC_QSPI
#else
#define LCD_INTERFACE                        LCD_INTERFACE_QSPI
#endif
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_SH8601Z_SPI)
#define LCD_INTERFACE                        LCD_INTERFACE_SPI
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_ST7265_RGB) || (TARGET_LCD_DEVICE == LCD_DEVICE_LCDC_A0500)
#define LCD_INTERFACE                        LCD_INTERFACE_LCDC_RGB
#endif

#if (TARGET_LCD_DEVICE == LCD_DEVICE_NT35110)
#define LCD_WIDTH                           480
#define LCD_HIGHT                           800
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_ST77916)
#define LCD_WIDTH                           385
#define LCD_HIGHT                           320
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_ST7801)
#define LCD_WIDTH                           368
#define LCD_HIGHT                           448
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_ST7265_RGB)
#define LCD_WIDTH                           800
#define LCD_HIGHT                           480
#elif (TARGET_LCD_DEVICE == LCD_DEVICE_LCDC_A0500)
#define LCD_WIDTH                           800
#define LCD_HIGHT                           480
#else
#define LCD_WIDTH                           454
#define LCD_HIGHT                           454
#endif

#ifdef __cplusplus
}
#endif

#endif /* _APP_GUI_H_ */
