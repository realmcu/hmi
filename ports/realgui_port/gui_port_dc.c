/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "guidef.h"
#include "gui_port.h"
#include "gui_api.h"
#include "trace.h"
#include "string.h"
#include "platform_utils.h"
#include "os_sched.h"
#include "trace.h"
#include "os_mem.h"
#include "rtl876x_pinmux.h"
#include "rtl_lcdc.h"
#include "system_status_api.h"
#include "fmc_api_ext.h"
#include "section.h"
//#include "lcd_st7265_800480_rgb.h"
#include "drv_lcd.h"
#include "lcd_st77916_360_360_qspi.h"

#define LCD_SECTION_HEIGHT                      10

#include <rtl876x_rcc.h>
#include <rtl876x_gdma.h>
#include <dma_channel.h>

#define PSRAM_FRAME_BUF1_ADDR               0x4000000
#define PSRAM_FRAME_BUF2_ADDR               (0x4000000 + 360 * 360 * 2)
static uint32_t current_buffer = PSRAM_FRAME_BUF1_ADDR;
static uint8_t dma_num = 0xa5, copy_num = 0xa5;

#if 0

static uint8_t copy_num = 0xa5;
static void rect_copy_by_dma_init(void)
{
    if (!GDMA_channel_request(&copy_num, NULL, false))
    {
        GUI_ASSERT("no dma for rect copy");
        return;
    }
    gui_log("rect copy dma num %d", copy_num);

}

static void rect_copy_by_dma_set_window(uint8_t *target, \
                                        uint32_t target_w/*same as screen w*/, \
                                        uint32_t target_h/*same as screen w*/, \
                                        uint32_t x, uint32_t y, uint32_t w, uint32_t h, \
                                        uint8_t *source)
{
    RCC_PeriphClockCmd(APBPeriph_GDMA, APBPeriph_GDMA_CLOCK, ENABLE);
    GDMA_ChannelTypeDef *dma_channel = DMA_CH_BASE(copy_num);
    GDMA_InitTypeDef GDMA_InitStruct;
    /*--------------GDMA init-----------------------------*/
    GDMA_StructInit(&GDMA_InitStruct);
    GDMA_InitStruct.GDMA_ChannelNum          = copy_num;
    GDMA_InitStruct.GDMA_BufferSize          = w * h;
    GDMA_InitStruct.GDMA_DIR                 = GDMA_DIR_MemoryToMemory;
    GDMA_InitStruct.GDMA_SourceInc           = DMA_SourceInc_Inc;
    GDMA_InitStruct.GDMA_DestinationInc      = DMA_DestinationInc_Inc;
    GDMA_InitStruct.GDMA_SourceMsize         =
        GDMA_Msize_32;                         // 8 msize for source msize
    GDMA_InitStruct.GDMA_DestinationMsize    =
        GDMA_Msize_32;                         // 8 msize for destiantion msize
    GDMA_InitStruct.GDMA_DestinationDataSize =
        GDMA_DataSize_HalfWord;                   // 32 bit width for destination transaction
    GDMA_InitStruct.GDMA_SourceDataSize      =
        GDMA_DataSize_HalfWord;                   // 32 bit width for source transaction
    GDMA_InitStruct.GDMA_SourceAddr          = (uint32_t)source;
    GDMA_InitStruct.GDMA_DestinationAddr     = (uint32_t)target + w * (y - 1) + x;

    GDMA_InitStruct.GDMA_Scatter_En         = ENABLE;
    GDMA_InitStruct.GDMA_ScatterCount       = w;
    GDMA_InitStruct.GDMA_ScatterInterval    = target_w - w;

    GDMA_Init(dma_channel, &GDMA_InitStruct);

    GDMA_INTConfig(copy_num, GDMA_INT_Transfer, ENABLE);
    GDMA_Cmd(copy_num, ENABLE);
}

static void wait_rect_copy_by_dma_done(void)
{
    while (GDMA_GetTransferINTStatus(copy_num) != SET);
    GDMA_ClearINTPendingBit(copy_num, GDMA_INT_Transfer);
}
#endif

static void gdma_start_transfer(uint8_t *dst, uint8_t *buf, uint32_t len)
{
    //fmc_flash_set_seq_trans(FMC_FLASH_NOR_IDX0, true);
    RCC_PeriphClockCmd(APBPeriph_GDMA, APBPeriph_GDMA_CLOCK, ENABLE);
    GDMA_ChannelTypeDef *dma_channel = DMA_CH_BASE(dma_num);
//    GDMA_ChannelTypeDef *support_channel = DMA_CH_BASE(support_dma_num);
//    GDMA_InitTypeDef SP_GDMA_InitStruct;
    GDMA_InitTypeDef RX_GDMA_InitStruct;
    /*--------------GDMA init-----------------------------*/
    GDMA_StructInit(&RX_GDMA_InitStruct);
    RX_GDMA_InitStruct.GDMA_ChannelNum          = dma_num;
    RX_GDMA_InitStruct.GDMA_BufferSize          = len / 2;
    RX_GDMA_InitStruct.GDMA_DIR                 = GDMA_DIR_MemoryToMemory;
    RX_GDMA_InitStruct.GDMA_SourceInc           = DMA_SourceInc_Inc;
    RX_GDMA_InitStruct.GDMA_DestinationInc      = DMA_DestinationInc_Inc;
    RX_GDMA_InitStruct.GDMA_SourceMsize         =
        GDMA_Msize_8;                         // 8 msize for source msize
    RX_GDMA_InitStruct.GDMA_DestinationMsize    =
        GDMA_Msize_8;                         // 8 msize for destiantion msize
    RX_GDMA_InitStruct.GDMA_DestinationDataSize =
        GDMA_DataSize_Word;                   // 32 bit width for destination transaction
    RX_GDMA_InitStruct.GDMA_SourceDataSize      =
        GDMA_DataSize_Word;                   // 32 bit width for source transaction
    RX_GDMA_InitStruct.GDMA_SourceAddr          = (uint32_t)buf;
    RX_GDMA_InitStruct.GDMA_DestinationAddr     = (uint32_t)dst;

    GDMA_Init(dma_channel, &RX_GDMA_InitStruct);
    GDMA_INTConfig(dma_num, GDMA_INT_Transfer, ENABLE);
    GDMA_Cmd(dma_num, ENABLE);
}

static void gdma_wait_transfer_done(void)
{
    while (GDMA_GetTransferINTStatus(dma_num) != SET);
    GDMA_ClearINTPendingBit(dma_num, GDMA_INT_Transfer);
}




void port_gui_lcd_update(struct gui_dispdev *dc)
{
    uint32_t i = dc->section_count;
    uint32_t total_section_cnt = dc->section_total;

    void *dst = (void *)(current_buffer + i * dc->fb_width * dc->fb_height * 2);

//    gui_log("%s %d  %d", __FUNCTION__, __LINE__, i);
    if (i == 0)
    {
        gdma_start_transfer(dst, dc->frame_buf, dc->fb_width * dc->fb_height);
    }
    else if (i == total_section_cnt - 1)
    {
        uint32_t last_height = dc->screen_height - dc->section_count * dc->fb_height;
        gdma_wait_transfer_done();
        gdma_start_transfer(dst, dc->frame_buf, dc->fb_width * last_height);
        gdma_wait_transfer_done();

        rtk_lcd_hal_transfer_done();
        // gui_log("%s %d", __FUNCTION__, __LINE__);
        rtk_lcd_hal_set_window(0, 0, dc->screen_width, dc->screen_height);
        // gui_log("%s %d", __FUNCTION__, __LINE__);

        // rtk_lcd_hal_update_framebuffer((uint8_t *)current_buffer, dc->screen_width * dc->screen_height);

        rtk_lcd_hal_start_transfer((uint8_t *)current_buffer, dc->screen_width * dc->screen_height);
        // rtk_lcd_hal_transfer_done();

        //    gui_log("%s %d", __FUNCTION__, __LINE__);

        if (current_buffer == PSRAM_FRAME_BUF1_ADDR)
        {
            current_buffer = PSRAM_FRAME_BUF2_ADDR;
        }
        else if (current_buffer == PSRAM_FRAME_BUF2_ADDR)
        {
            current_buffer = PSRAM_FRAME_BUF1_ADDR;
        }
        else
        {
            current_buffer = PSRAM_FRAME_BUF1_ADDR;
        }
    }
    else
    {
        gdma_wait_transfer_done();
        gdma_start_transfer(dst, dc->frame_buf, dc->fb_width * dc->fb_height);
    }
}
static struct gui_dispdev dc =
{
    .type = DC_RAMLESS,
    .section = {0, 0, 0, 0},
    .section_count = 0,

    .lcd_update = port_gui_lcd_update,

    .reset_lcd_timer = NULL,
    .get_lcd_us = NULL,

    .lcd_te_wait = NULL,
};

SHM_DATA_SECTION static uint8_t __attribute__((aligned(4))) __attribute__((
                                                                              used)) disp_write_buff1_port[360 * 10 * 2];
SHM_DATA_SECTION static uint8_t __attribute__((aligned(4))) __attribute__((
                                                                              used)) disp_write_buff2_port[360 * 10 * 2];

void gui_port_dc_init(void)
{
    if (!GDMA_channel_request(&dma_num, NULL, true))
    {
        GUI_ASSERT("no dma for psram");
        return;
    }
    if (!GDMA_channel_request(&copy_num, NULL, false))
    {
        GUI_ASSERT("no dma for rect copy");
        return;
    }

    dc.frame_buf = NULL;
    dc.fb_height = LCD_SECTION_HEIGHT;
    dc.fb_width = rtk_lcd_hal_get_width();
    dc.disp_buf_1 = disp_write_buff1_port;
    dc.disp_buf_2 = disp_write_buff2_port;
    dc.bit_depth = rtk_lcd_hal_get_pixel_bits();

    dc.screen_width =  rtk_lcd_hal_get_width();
    dc.screen_height = rtk_lcd_hal_get_height();

    gui_dc_info_register(&dc);
    gui_log("gui_port_dc_init ");
    gui_log("dc addr is 0x%x,line is %d", &dc, __LINE__);

}

