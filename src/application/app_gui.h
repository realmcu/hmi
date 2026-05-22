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
#include "flash_map.h"

#define LCD_INTERFACE_QSPI                   0
#define LCD_INTERFACE_8080                   1
#define LCD_INTERFACE_QSPI_1_BIT             2
#define LCD_INTERFACE_LCDC_QSPI              3
#define LCD_INTERFACE_SPI                    4

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
#define USE_LCD_DEVICE_ST7265_RGB 1
// Set to 1 to choose LCD_DEVICE_LCDC_A0500
#define USE_LCD_DEVICE_A0500_RGB  0
// Set to 1 to choose LCD_DEVICE_LCDC__SH8601Z
#define USE_LCD_DEVICE_LCD_DEVICE_SH8601Z_LCDC_QSPI  0

#if ((CONFIG_SOC_SERIES_RTL8773E == 1) || (CONFIG_SOC_SERIES_RTL8773D== 1) )&& (USE_LCD_DEVICE_ST7265_RGB == 1)
#define TARGET_LCD_DEVICE                    LCD_DEVICE_ST7265_RGB
#define TARGET_TOUCH_DEVICE                  TOUCH_DEVICE_INVALID
#elif ((CONFIG_SOC_SERIES_RTL8773E == 1) || (CONFIG_SOC_SERIES_RTL8773D== 1) ) && (USE_LCD_DEVICE_A0500_RGB == 1)
#define TARGET_LCD_DEVICE                    LCD_DEVICE_LCDC_A0500
#define TARGET_TOUCH_DEVICE                  TOUCH_DEVICE_INVALID
#elif ((CONFIG_SOC_SERIES_RTL8773E == 1) || (CONFIG_SOC_SERIES_RTL8773D== 1) ) && (USE_LCD_DEVICE_LCD_DEVICE_SH8601Z_LCDC_QSPI == 1)
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
#define PAGE_SWITCH_TIMER_INTERVAL          40
#define LCD_SECTION_HEIGHT                  10
#define FEATURE_PSRAM                       1

#define RGB16BIT_565                        16
#define RGB24BIT_888                        24
#define PIXEL_FORMAT                        RGB16BIT_565
#define PIXEL_BYTES                         (2)
#define LCD_SECTION_BYTE_LEN                (LCD_WIDTH * LCD_SECTION_HEIGHT * PIXEL_BYTES)
#define MAX_SECTION_COUNT                   (0x1FFFF/LCD_SECTION_BYTE_LEN)
#define TOTAL_SECTION_COUNT                 (LCD_HIGHT / LCD_SECTION_HEIGHT + ((LCD_HIGHT % LCD_SECTION_HEIGHT)?1:0))

#if ((TARGET_RTL87X3E == 1) || (CONFIG_SOC_SERIES_RTL8773E == 1))
#define ENABLE_PSRAM_FOR_LCD                (1)
#elif (CONFIG_SOC_SERIES_RTL8773D== 1)
#define ENABLE_PSRAM_FOR_LCD                (0)
#endif

#define ENABLE_TE_FOR_LCD                   (0)

#if (PIXEL_FORMAT == RGB24BIT_888)&&(LCD_WIDTH % 4 != 0)
#error "DMA TRANS LIMIT!"
#endif

#define RED                                 (0xf800)
#define BLUE                                (0x001f)

typedef struct
{
    uint16_t lcd_width;
    uint16_t lcd_hight;
    uint16_t lcd_section_height;
    uint16_t pixel_format;
    uint16_t pixel_bytes;
    uint16_t lcd_section_byte_len;
    uint16_t max_section_count;
    uint16_t total_section_count;
} T_RTK_GENERAL_GUI_CONFIG;

extern const T_RTK_GENERAL_GUI_CONFIG rtk_gui_config;

#define FONT_DATA_ADDR USER_DATA1_ADDR

#define LCD_FRAME_SYNC            P2_1
#define LCD_FS_GPIO_PORT          GPIOA
#define LCD_FS_GPIO_PIN_BIT       BIT16
#define LCD_FS_GPIO_IRQ           GPIO_A_16_23_IRQn

#define QSPI_LCD_POWER            P2_2
#define BL_PWM_TIM                TIM5

#define LCD_8080_RST              P2_3

#ifdef __cplusplus
}
#endif

#endif /* _APP_GUI_H_ */
