
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


#define DRV_LCD_WIDTH   360
#define DRV_LCD_HIGHT   360
#if 1
#include "lcd_st77916_360_360_qspi.h"


#ifdef DRV_LCD_WIDTH
#undef DRV_LCD_WIDTH
#define DRV_LCD_WIDTH   360
#endif

#ifdef DRV_LCD_HIGHT
#undef DRV_LCD_HIGHT
#define DRV_LCD_HIGHT   360
#endif

#else

#include "lcd_icna3310_466_466_qspi.h"

#ifdef DRV_LCD_WIDTH
#undef DRV_LCD_WIDTH
#define DRV_LCD_WIDTH   466
#endif

#ifdef DRV_LCD_HIGHT
#undef DRV_LCD_HIGHT
#define DRV_LCD_HIGHT   466
#endif

#endif





#define LCD_SECTION_HEIGHT                      20


#include <rtl876x_rcc.h>
#include <rtl876x_gdma.h>
#include <dma_channel.h>

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




void port_gui_lcd_update(struct gui_dispdev *dc)
{
    uint32_t total_section_cnt = (rtk_lcd_hal_get_height() / LCD_SECTION_HEIGHT + ((
            rtk_lcd_hal_get_height() % LCD_SECTION_HEIGHT) ? 1 : 0));

    if (dc->section_count == 0)
    {
        rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
        rtk_lcd_hal_set_window(0, dc->fb_height * dc->section_count, dc->fb_width, dc->fb_height);
        rtk_lcd_hal_start_transfer(dc->frame_buf, dc->fb_width * dc->fb_height);
    }
    else if (dc->section_count == total_section_cnt - 1)
    {
        uint32_t last_height = dc->screen_height - dc->section_count * dc->fb_height;
        rtk_lcd_hal_transfer_done();
        rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
        rtk_lcd_hal_set_window(0, dc->fb_height * dc->section_count, dc->fb_width, last_height);
        rtk_lcd_hal_start_transfer(dc->frame_buf, dc->fb_width * last_height);
        rtk_lcd_hal_transfer_done();
    }
    else
    {
        rtk_lcd_hal_transfer_done();
        rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
        rtk_lcd_hal_set_window(0, dc->fb_height * dc->section_count, dc->fb_width, dc->fb_height);
        rtk_lcd_hal_start_transfer(dc->frame_buf, dc->fb_width * dc->fb_height);
    }

}
static void gui_dc_lcd_power_on(void)
{
    gui_log("port_gui_lcd_power_on");
}
static void gui_dc_lcd_power_off(void)
{
    gui_log("port_gui_lcd_power_off");
}
static struct gui_dispdev dc =
{
    .type = DC_RAMLESS,
    .section = {0, 0, 0, 0},
    .section_count = 0,

    .lcd_update = port_gui_lcd_update,
    .lcd_power_off = gui_dc_lcd_power_off,
    .lcd_power_on = gui_dc_lcd_power_on,

    .reset_lcd_timer = NULL,
    .get_lcd_us = NULL,

    .lcd_te_wait = NULL,

};

#if 1
DSP_RAM_BSS_SECTION static uint8_t __attribute__((aligned(4))) __attribute__((
        used)) disp_write_buff1_port[DRV_LCD_WIDTH *
                                                   LCD_SECTION_HEIGHT * 2];
DSP_RAM_BSS_SECTION static uint8_t __attribute__((aligned(4))) __attribute__((
        used)) disp_write_buff2_port[DRV_LCD_WIDTH *
                                                   LCD_SECTION_HEIGHT * 2];
#else
static uint8_t __attribute__((aligned(4))) __attribute__((
                                                             used)) disp_write_buff1_port[DRV_LCD_WIDTH *
                                                                                   LCD_SECTION_HEIGHT * 2];
static uint8_t __attribute__((aligned(4))) __attribute__((
                                                             used)) disp_write_buff2_port[DRV_LCD_WIDTH *
                                                                                   LCD_SECTION_HEIGHT * 2];
#endif

void gui_port_dc_init(void)
{
    // rtk_lcd_hal_init();
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

