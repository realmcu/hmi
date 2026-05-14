
#include "guidef.h"
#include "gui_port.h"
#include "gui_api.h"
#include "string.h"
#include "st7262.h"
#include "ameba_gdma.h"
#include "ameba_soc.h"
#include "os_wrapper.h"
#include <stdio.h>
#include "dma_api.h"

#define DRV_LCD_WIDTH   800
#define DRV_LCD_HIGHT   480
#define DRV_LCD_BITS   16
#define USE_PFB         1
#if USE_PFB
#define LCD_SECTION_HEIGHT  30
static uint8_t *g_buffer_lcd = NULL;
#endif
gdma_t dma_obj;
volatile bool dma_memcpy_done = false;
static int g_width = 0;
static int g_height = 0;
static uint8_t *g_buffer_0 = NULL;
static uint8_t *g_buffer_1 = NULL;
static rtos_sema_t g_vsync_sem;
volatile int wait_for_vsync = 0;

void port_gui_lcd_update(struct gui_dispdev *dc)
{
#if USE_PFB
    dc->frame_buf = g_buffer_lcd;
#endif
	st7262_clean_invalidate_buffer(dc->frame_buf);
	wait_for_vsync = 1;
	rtos_sema_take(g_vsync_sem, RTOS_MAX_TIMEOUT);
	wait_for_vsync = 0;

}

#if USE_PFB
static uint8_t section1[DRV_LCD_WIDTH * LCD_SECTION_HEIGHT * DRV_LCD_BITS / 8];
static uint8_t section2[DRV_LCD_WIDTH * LCD_SECTION_HEIGHT * DRV_LCD_BITS / 8];
static uint32_t psram_offset = 0;
extern void memcpy_gdma_wait_done(void);
extern int memcpy_gdma_no_wait(void *dest, void *src, u32 size);
static void gdma_start_transfer(uint8_t *buf, uint32_t len)
{
    // DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    // memcpy(g_buffer_lcd + psram_offset * DRV_LCD_BITS / 8, buf, len * DRV_LCD_BITS / 8);
    // memcpy_gdma(g_buffer_lcd + psram_offset * DRV_LCD_BITS / 8, buf, len * DRV_LCD_BITS / 8);
    //memcpy_gdma_no_wait(g_buffer_lcd + psram_offset * DRV_LCD_BITS / 8, buf, len * DRV_LCD_BITS / 8);
    uint32_t data_size = len * DRV_LCD_BITS / 8;
    if(data_size <= 16384)
    {
        DCache_CleanInvalidate((uint32_t)(uintptr_t)buf, data_size);
    }
    else {
        DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    }
    dma_memcpy_done = false;
    dma_memcpy(&dma_obj, g_buffer_lcd + psram_offset * DRV_LCD_BITS / 8, buf, data_size);
}

static void gdma_wait_transfer_done(void)
{
    while(!dma_memcpy_done);
}


void port_gui_lcd_update_nofb(struct gui_dispdev *dc)
{
    (void)dc;
    uint32_t total_section_cnt = (DRV_LCD_HIGHT / LCD_SECTION_HEIGHT + ((
            DRV_LCD_HIGHT % LCD_SECTION_HEIGHT) ? 1 : 0));
    if (dc->section_count == 0)
    {
        gdma_start_transfer(dc->frame_buf, dc->fb_width * dc->fb_height);
        psram_offset += dc->fb_width * dc->fb_height;
    }
    else if (dc->section_count == total_section_cnt - 1)
    {
        gdma_wait_transfer_done();
        gdma_start_transfer(dc->frame_buf, dc->fb_width * dc->fb_height);
        gdma_wait_transfer_done();
        psram_offset = 0;
        port_gui_lcd_update(dc);

        if ((uint32_t)g_buffer_lcd == (uint32_t)g_buffer_0)
        {
            g_buffer_lcd = g_buffer_0;
        }
        else
        {
            g_buffer_lcd = g_buffer_1;
        }
    }
    else
    {
        gdma_wait_transfer_done();
        gdma_start_transfer(dc->frame_buf, dc->fb_width * dc->fb_height);
        psram_offset += dc->fb_width * dc->fb_height;
    }
}
#endif

static struct gui_dispdev dc =
{
#if USE_PFB
	.type = DC_RAMLESS,
    .lcd_update = port_gui_lcd_update_nofb,
#else
    .type = DC_DOUBLE,
    .lcd_update = port_gui_lcd_update,
#endif
	.section = {0, 0, 0, 0},
	.section_count = 0,

	.flash_seq_trans_disable = NULL,
	.flash_seq_trans_enable = NULL,
	.reset_lcd_timer = NULL,
	.get_lcd_us = NULL,

	.lcd_te_wait = NULL,
	//TODO: remove when ppe is ready
	.lcd_draw_sync = NULL, //PPEV2_Finish,

};

static void display_vsync_handle(void *data)
{
    UNUSED(data);
    if (wait_for_vsync == 1) {
        rtos_sema_give(g_vsync_sem);
    }
}


static void spic1_psram_speed_report(uint8_t* p_buf)
{
#define SECTION_SIZE 1024
    static uint32_t volatile test_buffer[SECTION_SIZE];


    uint32_t total_time = 0;
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t old_stamp = rtos_time_get_current_system_time_us();
        memcpy((void *)test_buffer, (void *)(p_buf + i * SECTION_SIZE * 4),
               sizeof(uint32_t)*SECTION_SIZE);
        uint32_t new_stamp = rtos_time_get_current_system_time_us();
        total_time = total_time + new_stamp - old_stamp;
        for (uint32_t j = 0; j < 1024; j++)
        {
            checksum += test_buffer[j];
        }
    }
    gui_log("[Checksum] %u\n", checksum);

    uint32_t time_ms = total_time / 1000;
    uint32_t time_us = total_time % 1000;

    gui_log("[PSRAM Read Speed Report]  Read 1M byte t=%d.%d ms \n",
                time_ms, time_us);
    gui_log("[PSRAM Read Speed Report] %dMByte/S \n",
                1000000 / total_time);
}

static void spic_nor_flash_speed_report(void)
{
#define SECTION_SIZE 1024
    static uint32_t volatile test_buffer[SECTION_SIZE];


    uint32_t total_time = 0;
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t old_stamp = rtos_time_get_current_system_time_us();
        memcpy((void *)test_buffer, (void *)(0x08768440 + i * SECTION_SIZE * 4),
               sizeof(uint32_t)*SECTION_SIZE);
        uint32_t new_stamp = rtos_time_get_current_system_time_us();
        total_time = total_time + new_stamp - old_stamp;
        for (uint32_t j = 0; j < 1024; j++)
        {
            checksum += test_buffer[j];
        }
    }
    gui_log("[Checksum] %u\n", checksum);

    uint32_t time_ms = total_time / 1000;
    uint32_t time_us = total_time % 1000;

    gui_log("[Nor Flash Read Speed Report] Read 1M byte t=%d.%d ms\n",
               time_ms, time_us);
    gui_log("[Nor Flash Read Speed Report] %dMByte/S\n",
               1000000 / total_time);
}

u32 memcpy_by_gdma_int(void* param)
{
    (void)param;
    dma_memcpy_done = true;
    return 0;
}

void gui_port_dc_init(void)
{
    printf("gui_port_dc_init with st7262 driver\n");
    dc.frame_buf = NULL;
#if USE_PFB
    dc.fb_height = LCD_SECTION_HEIGHT;
#else
    dc.fb_height = DRV_LCD_HIGHT;
#endif
    dc.fb_width = DRV_LCD_WIDTH;

    dc.bit_depth = DRV_LCD_BITS;

    dc.screen_width =  DRV_LCD_WIDTH;
    dc.screen_height = DRV_LCD_HIGHT;    

    gui_dc_info_register(&dc);
    
	st7262_init(RGB565);
	st7262_get_info(&g_width, &g_height);
    g_buffer_0 = (uint8_t *)malloc(g_width * g_height * DRV_LCD_BITS / 8 + 100);
    memset(g_buffer_0, 0xFF, g_width * g_height * DRV_LCD_BITS / 8 + 100);
    // g_buffer_0 = g_buffer_0 + 64 - ((uintptr_t)g_buffer_0 % 64);
    g_buffer_1 = (uint8_t *)malloc(g_width * g_height * DRV_LCD_BITS / 8 + 100);
    // g_buffer_1 = g_buffer_1 + 64 - ((uintptr_t)g_buffer_1 % 64);
    dc.frame_buf = g_buffer_0;
#if USE_PFB
    g_buffer_lcd = g_buffer_0;
    dc.disp_buf_1 = section1;
    dc.disp_buf_2 = section2;
    gui_log("gui_port_dc_init, section1 = 0x%x section2 = 0x%x\n", section1, section2);
#else
	dc.frame_buf = g_buffer_0;
	dc.disp_buf_1 = g_buffer_0;
    dc.disp_buf_2 = g_buffer_1;
#endif
	gui_log("gui_port_dc_init, g_buffer_0 = 0x%x g_buffer_0 = 0x%x\n", g_buffer_0, g_buffer_1);

    dma_memcpy_init(&dma_obj, memcpy_by_gdma_int, 0);
	spic1_psram_speed_report(g_buffer_0);
    spic_nor_flash_speed_report();
    ST7262VBlankCallback *callback =    (ST7262VBlankCallback *)malloc(sizeof(ST7262VBlankCallback));
	rtos_sema_create(&g_vsync_sem, 0, RTOS_SEMA_MAX_COUNT);
    callback->VBlank = display_vsync_handle;
    st7262_register_callback(callback, NULL);
    
	st7262_clean_invalidate_buffer(g_buffer_0);
}
