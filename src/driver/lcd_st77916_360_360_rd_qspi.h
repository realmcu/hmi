/**
*****************************************************************************************
*     Copyright(c) 2025, Realtek Semiconductor Corporation. All rights reserved.

*
*     SPDX-License-Identifier: Apache-2.0
*****************************************************************************************
* @file    lcd_st77916_320_385_qspi.c
* @brief   This file provides ST77916 LCD driver functions
* @author
* @date
* @version v1.0
* *************************************************************************************
*/

#ifndef __LCD_ST77916_360_360_RD_QSPI_H__
#define __LCD_ST77916_360_360_RD_QSPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define TE_VALID                            0

#define OUTPUT_PIXEL_BYTES                  2

#if OUTPUT_PIXEL_BYTES == 3
#define INPUT_PIXEL_BYTES                   4
#elif OUTPUT_PIXEL_BYTES == 2
#define INPUT_PIXEL_BYTES                   2
#endif

#if INPUT_PIXEL_BYTES == 3
#error "LCDC DMA doesn't allow 3 bytes input"
#endif


#define ST77916_360_360_LCD_WIDTH           360
#define ST77916_360_360_LCD_HEIGHT          360
#define ST77916_360_360_DRV_PIXEL_BITS      (INPUT_PIXEL_BYTES * 8)








typedef enum
{
    LCDC_TE_TYPE_NO_TE = 0x00,
    LCDC_TE_TYPE_HW_TE = 0x01,
    LCDC_TE_TYPE_SW_TE = 0x02,
} T_LCDC_TE_TYPE;


void rtk_lcd_hal_init(void);
void rtk_lcd_hal_set_window(uint16_t xStart, uint16_t yStart, uint16_t w, uint16_t h);
void rtk_lcd_hal_update_framebuffer(uint8_t *buf, uint32_t len);
void rtk_lcd_hal_clear_screen(uint32_t ARGB_color);
void rtk_lcd_hal_start_transfer(uint8_t *buf, uint32_t len);
void rtk_lcd_hal_transfer_done(void);


void rtk_lcd_hal_set_TE_type(T_LCDC_TE_TYPE state);
uint32_t rtk_lcd_hal_get_width(void);
uint32_t rtk_lcd_hal_get_height(void);
uint32_t rtk_lcd_hal_get_pixel_bits(void);


#ifdef __cplusplus
}
#endif
#endif /* _LCD_ST77916_360_360_LCDC_H_ */
