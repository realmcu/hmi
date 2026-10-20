/**
 * @file lv_port_disp.c
 *
 * Display port for RTL8762D + lcd_st77916g5_320_qspi (Wearable/device/lcd/8762D)
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include "drv_lcd.h"
#include "trace.h"

/*********************
 *      DEFINES
 *********************/
#define LV_DOUBLE_BUFFER       1

/* Max resolution of the panel, used to size the draw buffers.
 * Real resolution is read from drv_lcd_get_screen_width/height() */
#define MY_DISP_MAX_HOR_RES    385
#define MY_DISP_VER_BUF        10

/* One GDMA block can carry at most 4095 data units. drv_lcd_start_transfer() of
 * st77916 uses half word units (len >> 1), so one flush must be <= 4095 * 2 pixels */
#define MY_DISP_DMA_MAX_PIXELS (4095 * 2)

#define MY_DISP_BUF_SIZE       (MY_DISP_MAX_HOR_RES * MY_DISP_VER_BUF * LV_COLOR_DEPTH / 8)

#if (MY_DISP_MAX_HOR_RES * MY_DISP_VER_BUF) > MY_DISP_DMA_MAX_PIXELS
#error "draw buffer is larger than one GDMA block, reduce MY_DISP_VER_BUF"
#endif

#if LV_DOUBLE_BUFFER
static uint8_t __attribute__((aligned(4))) partial_frame_buffer_lcd1[MY_DISP_BUF_SIZE];
static uint8_t __attribute__((aligned(4))) partial_frame_buffer_lcd2[MY_DISP_BUF_SIZE];

#define LV_PORT_BUF1       (void *)partial_frame_buffer_lcd1
#define LV_PORT_BUF2       (void *)partial_frame_buffer_lcd2
#else
static uint8_t __attribute__((aligned(4))) partial_frame_buffer_lcd[MY_DISP_BUF_SIZE];

#define LV_PORT_BUF        (void *)partial_frame_buffer_lcd
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void disp_flush_wait(lv_display_t *disp);

/**********************
 *      MACROS
 **********************/

/**********************
 *     VARIBALES
 **********************/
static volatile bool disp_transfer_pending = false;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*------------------------------------
     * Create a display and set a flush_cb
     * -----------------------------------*/
    uint32_t hor_res = drv_lcd_get_screen_width();
    uint32_t ver_res = drv_lcd_get_screen_height();
    if (hor_res > MY_DISP_MAX_HOR_RES)
    {
        DBG_DIRECT("lv_port_disp_init: hor res %d > MY_DISP_MAX_HOR_RES", hor_res);
        while (1);
    }

    lv_display_t *disp = lv_display_create(hor_res, ver_res);
    /* st77916 takes RGB565 high byte first, same as LV_COLOR_16_SWAP in lvgl v8 */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_flush_wait_cb(disp, disp_flush_wait);

#if LV_DOUBLE_BUFFER
    lv_display_set_buffers(disp, LV_PORT_BUF1, LV_PORT_BUF2, MY_DISP_BUF_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
    lv_display_set_buffers(disp, LV_PORT_BUF, NULL, MY_DISP_BUF_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /* lcd is inited by hw_lcd_init() in display_components_init() */
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/* Called by LVGL while disp->flushing is set: before the next flush (double buffer) or
 * before rendering into the buffer again (single buffer). LVGL clears flushing after it. */
static void disp_flush_wait(lv_display_t *disp)
{
    LV_UNUSED(disp);
    if (disp_transfer_pending)
    {
        drv_lcd_transfer_done();
        disp_transfer_pending = false;
    }
}

/*Flush the content of the internal buffer the specific area on the display.
 *The DMA runs in the background while LVGL renders into the other buffer,
 *disp_flush_wait() waits for it. So lv_display_flush_ready() is not called here.*/
static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    if (!disp_flush_enabled)
    {
        lv_display_flush_ready(disp_drv);
        return;
    }

    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    drv_lcd_set_window(area->x1, area->y1, w, h);
    drv_lcd_start_transfer(px_map, w * h);
    disp_transfer_pending = true;
}

#else /*Enable this file at the top*/

/*This dummy typedef exsts purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
