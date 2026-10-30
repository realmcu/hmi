#include <string.h>
#include "guidef.h"
#include "gui_port.h"
#include "gui_api.h"
#include "trace.h"
#include "platform_utils.h"
#include "os_sched.h"
#include "os_mem.h"
#include "rtl876x_pinmux.h"
#include "rtl_lcdc.h"
#include "system_status_api.h"
#include "fmc_api_ext.h"
#include "section.h"
#include "app_power_manage.h"
#include "app_dlps.h"
#include "lcd_st7801n_466_466_qspi.h"
#include "dma_channel.h"
#include "rtl_ppe.h"
#include "ebadge_psram_map.h"

#ifdef DRV_LCD_WIDTH
    #undef DRV_LCD_WIDTH
#endif
#ifdef DRV_LCD_HEIGHT
    #undef DRV_LCD_HEIGHT
#endif

#define DRV_LCD_WIDTH   466
#define DRV_LCD_HEIGHT  466

#define LCD_SECTION_HEIGHT                   20
#define LCD_USE_PFB                          1

#define PSRAM_GUI_START_ADDR                EB_PSRAM_DC_FRAMBUFFER_BASE
#define PSRAM_FRAME_BUF1_ADDR               PSRAM_GUI_START_ADDR
#define PSRAM_FRAME_BUF2_ADDR               (PSRAM_FRAME_BUF1_ADDR + DRV_LCD_WIDTH * DRV_LCD_HEIGHT * INPUT_PIXEL_BYTES)
#define PSRAM_GUI_HEAP_ADDR                 (PSRAM_FRAME_BUF1_ADDR + DRV_LCD_WIDTH * DRV_LCD_HEIGHT * INPUT_PIXEL_BYTES * 2)


bool is_screenon = false;

#if !LCD_USE_PFB
#if LCD_SECTION_HEIGHT != DRV_LCD_HEIGHT
#include <rtl876x_rcc.h>
#include <rtl876x_gdma.h>

static uint32_t psram_offset = 0;
static uint8_t *p_psram_addr = (uint8_t *)PSRAM_FRAME_BUF1_ADDR;
static uint8_t dma_num = 0xA5;

static void gdma_start_transfer(uint8_t *source, uint8_t *destination, uint32_t len)
{
    RCC_PeriphClockCmd(APBPeriph_GDMA, APBPeriph_GDMA_CLOCK, ENABLE);
    GDMA_ChannelTypeDef *dma_channel = DMA_CH_BASE(dma_num);
    GDMA_InitTypeDef RX_GDMA_InitStruct;
    /*--------------GDMA init-----------------------------*/
    GDMA_StructInit(&RX_GDMA_InitStruct);
    RX_GDMA_InitStruct.GDMA_ChannelNum          = dma_num;
    RX_GDMA_InitStruct.GDMA_BufferSize          = len;
    RX_GDMA_InitStruct.GDMA_DIR                 = GDMA_DIR_MemoryToMemory;
    RX_GDMA_InitStruct.GDMA_SourceInc           = DMA_SourceInc_Inc;
    RX_GDMA_InitStruct.GDMA_DestinationInc      = DMA_DestinationInc_Inc;
    RX_GDMA_InitStruct.GDMA_SourceMsize         = GDMA_Msize_16;
    RX_GDMA_InitStruct.GDMA_DestinationMsize    = GDMA_Msize_16;
    RX_GDMA_InitStruct.GDMA_DestinationDataSize = GDMA_DataSize_Word;
    RX_GDMA_InitStruct.GDMA_SourceDataSize      = GDMA_DataSize_Word;
    RX_GDMA_InitStruct.GDMA_SourceAddr          = (uint32_t)source;
    RX_GDMA_InitStruct.GDMA_DestinationAddr     = (uint32_t)destination;

    GDMA_ClearINTPendingBit(dma_num, GDMA_INT_Transfer);
    GDMA_Init(dma_channel, &RX_GDMA_InitStruct);
    GDMA_INTConfig(dma_num, GDMA_INT_Transfer, ENABLE);
    GDMA_Cmd(dma_num, ENABLE);
}

static void gdma_wait_transfer_done(void)
{
    while (GDMA_GetTransferINTStatus(dma_num) != SET);
}

void port_gui_lcd_update(struct gui_dispdev *dc)
{
    rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
    
    uint32_t transfer_height = (dc->section_count == dc->section_total - 1) ? 
                               (dc->screen_height - dc->section_count * dc->fb_height) : 
                               dc->fb_height;
    
#if INPUT_PIXEL_BYTES == 2
    uint32_t transfer_size = dc->fb_width * transfer_height / 2;
#elif INPUT_PIXEL_BYTES == 3
    uint32_t transfer_size = dc->fb_width * transfer_height * 3 / 4;
#elif INPUT_PIXEL_BYTES == 4
    uint32_t transfer_size = dc->fb_width * transfer_height;
#endif

    if (dc->section_count > 0)
    {
        gdma_wait_transfer_done();
    }
    
    gdma_start_transfer(dc->frame_buf, p_psram_addr + psram_offset * INPUT_PIXEL_BYTES, transfer_size);
    psram_offset += dc->fb_width * transfer_height;
    
    if (dc->section_count == dc->section_total - 1)
    {
        gdma_wait_transfer_done();
        psram_offset = 0;
        rtk_lcd_hal_transfer_done();
        rtk_lcd_hal_set_window(0, 0, dc->screen_width, dc->screen_height);
        rtk_lcd_hal_start_transfer(p_psram_addr, dc->screen_width * dc->screen_height);

        p_psram_addr = ((uint32_t)p_psram_addr == PSRAM_FRAME_BUF1_ADDR) ? 
                       (uint8_t *)PSRAM_FRAME_BUF2_ADDR : 
                       (uint8_t *)PSRAM_FRAME_BUF1_ADDR;
    }
}
#else
void port_gui_lcd_update(struct gui_dispdev *dc)
{
    rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
    rtk_lcd_hal_transfer_done();
    // SCB_CleanInvalidateDCache();
    rtk_lcd_hal_set_window(0, dc->fb_height * dc->section_count, dc->fb_width, dc->fb_height);
    rtk_lcd_hal_start_transfer(dc->frame_buf, dc->fb_width * dc->fb_height);
}
#endif

#else
void port_gui_lcd_update(struct gui_dispdev *dc)
{
    uint32_t total_section_cnt = (rtk_lcd_hal_get_height() / LCD_SECTION_HEIGHT +
                                  ((rtk_lcd_hal_get_height() % LCD_SECTION_HEIGHT) ? 1 : 0));

    if (dc->section_count == 0)
    {
        rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
        rtk_lcd_hal_transfer_done();
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
#endif

#if LCD_SECTION_HEIGHT != DRV_LCD_HEIGHT
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
#endif

static void gui_dc_lcd_power_on(void)
{
    gui_log("port_gui_lcd_power_on\n");
    if (app_dlps_check_enter_bits(APP_DLPS_ENTER_CHECK_DISPLAY))
    {
        return;
    }
    /* Claim the DLPS gate *before* touching LCDC, not after. */
    app_dlps_disable(APP_DLPS_ENTER_CHECK_DISPLAY);
    app_screen_on_off_power_set(true);
    rtk_lcd_hal_init();
    is_screenon = true;
    sys_hall_auto_sleep_in_idle(false);
}

extern void rtk_lcd_hal_set_display(bool on);
static void gui_dc_lcd_power_off(void)
{
    gui_log("port_gui_lcd_power_off\n");
    if (!app_dlps_check_enter_bits(APP_DLPS_ENTER_CHECK_DISPLAY))
    {
        return;
    }
    platform_delay_ms(20);
    rtk_lcd_hal_set_display(false);
    is_screenon = false;
    app_dlps_enable(APP_DLPS_ENTER_CHECK_DISPLAY);
    app_screen_on_off_power_set(false);
    sys_hall_auto_sleep_in_idle(true);
}

static struct gui_dispdev dc =
{
#if LCD_SECTION_HEIGHT != DRV_LCD_HEIGHT
    .type = DC_RAMLESS,
#else
    .type = DC_DOUBLE,
#endif
    .section = {0, 0, 0, 0},
    .section_count = 0,
    .lcd_update = port_gui_lcd_update,
    .lcd_power_off = gui_dc_lcd_power_off,
    .lcd_power_on = gui_dc_lcd_power_on,
    .reset_lcd_timer = NULL,
    .get_lcd_us = NULL,
    .lcd_te_wait = NULL,
};

void gui_port_dc_init(void)
{
    dc.frame_buf = NULL;
    dc.fb_height = LCD_SECTION_HEIGHT;
    dc.fb_width = DRV_LCD_WIDTH;

#if LCD_SECTION_HEIGHT == DRV_LCD_HEIGHT
    dc.disp_buf_1 = (uint8_t *)PSRAM_FRAME_BUF1_ADDR;
    dc.disp_buf_2 = (uint8_t *)PSRAM_FRAME_BUF2_ADDR;
#else
    dc.disp_buf_1 = disp_write_buff1_port;
    dc.disp_buf_2 = disp_write_buff2_port;
#endif

    dc.bit_depth = rtk_lcd_hal_get_pixel_bits();
    dc.screen_width = DRV_LCD_WIDTH;
    dc.screen_height = DRV_LCD_HEIGHT;
    is_screenon = true;
    gui_dc_info_register(&dc);
    gui_log("gui_port_dc_init ");
    gui_log("dc addr is 0x%x,line is %d", &dc, __LINE__);
    app_dlps_disable(APP_DLPS_ENTER_CHECK_DISPLAY);

#if !LCD_USE_PFB && (LCD_SECTION_HEIGHT != DRV_LCD_HEIGHT)
    if (GDMA_channel_request(&dma_num, NULL, true))
    {
        DBG_DIRECT("dma_num: 0x%x", dma_num);
    }
#endif
}
