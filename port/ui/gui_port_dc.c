
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
#include "lcd_sh8601z_410_502_qspi.h"
#include "dma_channel.h"
#include "rtl_ppe.h"

#ifdef DRV_LCD_WIDTH
#undef DRV_LCD_WIDTH
#endif
#ifdef DRV_LCD_HEIGHT
#undef DRV_LCD_HEIGHT
#endif

#define LCD_SECTION_HEIGHT                      20
#define LCD_USE_PSRAM_FULL_FB                   1

#define FB_NO_ROTATE                0
#define FB_ROTATE_CLOCKWISE         1
#define FB_ROTATE_ANTI_CLOCKWISE    0

#if FB_NO_ROTATE
#define DRV_LCD_WIDTH   410
#define DRV_LCD_HEIGHT  502
#else
#define DRV_LCD_WIDTH   502
#define DRV_LCD_HEIGHT  410
#endif

#if (FB_NO_ROTATE + FB_ROTATE_CLOCKWISE + FB_ROTATE_ANTI_CLOCKWISE) != 1
#error "Exactly one of FB_NO_ROTATE / FB_ROTATE_CLOCKWISE / FB_ROTATE_ANTI_CLOCKWISE must be 1"
#endif

#if !LCD_USE_PSRAM_FULL_FB && !FB_NO_ROTATE
#error "Rotation requires LCD_USE_PSRAM_FULL_FB"
#endif

#define PSRAM_GUI_START_ADDR                SPIC1_MEM_BASE
#define PSRAM_FRAME_BUF1_ADDR               PSRAM_GUI_START_ADDR
#define PSRAM_FRAME_BUF2_ADDR               (PSRAM_FRAME_BUF1_ADDR + DRV_LCD_WIDTH * DRV_LCD_HEIGHT * INPUT_PIXEL_BYTES)

/* PPE works on the same pixel format as the LCDC input path. Keep the format and
 * the byte step of the buffer address in sync with INPUT_PIXEL_BYTES. */
#if INPUT_PIXEL_BYTES == 2
#define LCD_PPE_FORMAT      PPE_RGB565
#elif INPUT_PIXEL_BYTES == 3
#define LCD_PPE_FORMAT      PPE_RGB888
#elif INPUT_PIXEL_BYTES == 4
#define LCD_PPE_FORMAT      PPE_ARGB8888
#else
#error "Unsupported INPUT_PIXEL_BYTES for PPE rotation"
#endif

#if FB_ROTATE_CLOCKWISE
static void rotate_frame_buffer_PPE(uint8_t *src, uint8_t *dst,
                                    int src_width, int src_height,
                                    int dst_width, int dst_height,
                                    int dst_start, int dst_stride)
{
    ppe_buffer_t target, source;
    memset(&target, 0, sizeof(ppe_buffer_t));
    memset(&source, 0, sizeof(ppe_buffer_t));
    target.format = LCD_PPE_FORMAT;
    target.address = (uint32_t)dst + (dst_stride - dst_start - src_height) * INPUT_PIXEL_BYTES;
    target.width = dst_width;
    target.height = dst_height;
    target.stride = dst_stride;
    target.const_color = 0xFFFFFFFF;
    target.win_x_min = 0;
    target.win_x_max = target.width - 1;
    target.win_y_min = 0;
    target.win_y_max = target.height - 1;

    source.address = (uint32_t)src;
    source.opacity = 0xFF;
    source.width = src_width;
    source.height = src_height;
    source.stride = src_width;
    source.const_color = 0xFFFFFFFF;
    source.format = target.format;
    source.win_x_min = 0;
    source.win_x_max = target.width - 1;
    source.win_y_min = 0;
    source.win_y_max = target.height - 1;

    ppe_rect_t constraint = (ppe_rect_t) {.x1 = 0, .y1 = 0, .x2 = src_height - 1, .y2 = src_width - 1};
    ppe_matrix_t inverse;
    ppe_get_identity(&inverse);

    ppe_translate((src_height - 1) / 2.0f, (src_width - 1) / 2.0f, &inverse);
    ppe_rotate(90, &inverse);
    ppe_translate(-(src_width - 1) / 2.0f, -(src_height - 1) / 2.0f, &inverse);

    ppe_matrix_inverse(&inverse);
    PPE_Blit_Inverse(&target, &source, NULL, &inverse, &constraint, PPE_BLEND_BYPASS);
    PPE_Finish();
}
#elif FB_ROTATE_ANTI_CLOCKWISE
static void rotate_frame_buffer_PPE(uint8_t *src, uint8_t *dst,
                                    int src_width, int src_height,
                                    int dst_width, int dst_height,
                                    int dst_start, int dst_stride)
{
    ppe_buffer_t target, source;
    memset(&target, 0, sizeof(ppe_buffer_t));
    memset(&source, 0, sizeof(ppe_buffer_t));
    target.format = LCD_PPE_FORMAT;
    target.address = (uint32_t)dst + dst_start * INPUT_PIXEL_BYTES;
    target.width = dst_width;
    target.height = dst_height;
    target.stride = dst_stride;
    target.const_color = 0xFFFFFFFF;
    target.win_x_min = 0;
    target.win_x_max = target.width - 1;
    target.win_y_min = 0;
    target.win_y_max = target.height - 1;

    source.address = (uint32_t)src;
    source.opacity = 0xFF;
    source.width = src_width;
    source.height = src_height;
    source.stride = src_width;
    source.const_color = 0xFFFFFFFF;
    source.format = target.format;
    source.win_x_min = 0;
    source.win_x_max = target.width - 1;
    source.win_y_min = 0;
    source.win_y_max = target.height - 1;

    ppe_rect_t constraint = (ppe_rect_t) {.x1 = 0, .y1 = 0, .x2 = src_height - 1, .y2 = src_width - 1};
    ppe_matrix_t inverse;
    ppe_get_identity(&inverse);

    ppe_translate((src_height - 1) / 2.0f, (src_width - 1) / 2.0f, &inverse);
    ppe_rotate(-90, &inverse);
    ppe_translate(-(src_width - 1) / 2.0f, -(src_height - 1) / 2.0f, &inverse);

    ppe_matrix_inverse(&inverse);
    PPE_Blit_Inverse(&target, &source, NULL, &inverse, &constraint, PPE_BLEND_BYPASS);
    PPE_Finish();
}
#endif



#if LCD_USE_PSRAM_FULL_FB
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

#if FB_NO_ROTATE
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
    uint32_t height = (dc->section_count == dc->section_total - 1) ?
                      (dc->screen_height - dc->section_count * dc->fb_height) :
                      dc->fb_height;

    rotate_frame_buffer_PPE(dc->frame_buf, p_psram_addr,
                            dc->fb_width, height,
                            dc->screen_height, dc->screen_width,
                            dc->section_count * dc->fb_height,
                            dc->screen_height);


    if (dc->section_count == dc->section_total - 1)
    {
        rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
        rtk_lcd_hal_transfer_done();
        rtk_lcd_hal_set_window(0, 0, rtk_lcd_hal_get_width(), rtk_lcd_hal_get_height());
        rtk_lcd_hal_start_transfer(p_psram_addr, rtk_lcd_hal_get_width() * rtk_lcd_hal_get_height());
        rtk_lcd_hal_transfer_done();
        p_psram_addr = ((uint32_t)p_psram_addr == PSRAM_FRAME_BUF1_ADDR) ?
                       (uint8_t *)PSRAM_FRAME_BUF2_ADDR :
                       (uint8_t *)PSRAM_FRAME_BUF1_ADDR;
    }
}
#endif

#else
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
#endif

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
                                                   LCD_SECTION_HEIGHT * INPUT_PIXEL_BYTES];
DSP_RAM_BSS_SECTION static uint8_t __attribute__((aligned(4))) __attribute__((
        used)) disp_write_buff2_port[DRV_LCD_WIDTH *
                                                   LCD_SECTION_HEIGHT * INPUT_PIXEL_BYTES];
#else
static uint8_t __attribute__((aligned(4))) __attribute__((
                                                             used)) disp_write_buff1_port[DRV_LCD_WIDTH *
                                                                                   LCD_SECTION_HEIGHT * INPUT_PIXEL_BYTES];
static uint8_t __attribute__((aligned(4))) __attribute__((
                                                             used)) disp_write_buff2_port[DRV_LCD_WIDTH *
                                                                                   LCD_SECTION_HEIGHT * INPUT_PIXEL_BYTES];
#endif

void gui_port_dc_init(void)
{
    rtk_lcd_hal_init();
    dc.frame_buf = NULL;
    dc.fb_height = LCD_SECTION_HEIGHT;
    dc.fb_width = DRV_LCD_WIDTH;

    dc.disp_buf_1 = disp_write_buff1_port;
    dc.disp_buf_2 = disp_write_buff2_port;

    dc.bit_depth = rtk_lcd_hal_get_pixel_bits();
    dc.screen_width =  DRV_LCD_WIDTH;
    dc.screen_height = DRV_LCD_HEIGHT;

    gui_dc_info_register(&dc);
    gui_log("gui_port_dc_init ");
    gui_log("dc addr is 0x%x,line is %d", &dc, __LINE__);

#if LCD_USE_PSRAM_FULL_FB
    if (GDMA_channel_request(&dma_num, NULL, true))
    {
        DBG_DIRECT("dma_num: 0x%x", dma_num);
    }
#endif

}

