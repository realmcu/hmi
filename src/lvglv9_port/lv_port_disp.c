/**
 * @file lv_port_disp.c
 *
 * LVGL v9 display port for RTL87x2G.
 * Panel is chosen in menu_config.h:
 *   CONFIG_REALTEK_INCA3311_280456_QSPI   280x456 QSPI (GRAM, no TE-less)
 *   CONFIG_REALTEK_ST7701S_480480_RGB     480x480 RGB/eDPI (continuous scanout)
 *
 * Both panels use the same data path: rtk_lcd_hal_update_framebuffer().
 * For RGB the panel scans the buffer out continuously, so DIRECT mode with two
 * screen sized buffers is required either way.
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include "display_drv.h"
#include "trace.h"

/*********************
 *      DEFINES
 *********************/
#define MY_DISP_HOR_RES     DISPLAY_DRV_WIDTH
#define MY_DISP_VER_RES     DISPLAY_DRV_HEIGHT

/* PSRAM layout (0x08000000, 4MB):
 *   buf1 / buf2 are 512KB apart, the largest frame is 480*480*2 = 450KB
 *   LVGL heap pool is at 0x08100000 (lv_mem_add_pool in lvgl_demo.c)
 */
#define LV_PORT_BUF1        ((void *)0x08000000u)
#define LV_PORT_BUF2        ((void *)0x08080000u)

#define MY_DISP_BUF_SIZE    (MY_DISP_HOR_RES * MY_DISP_VER_RES * DISPLAY_DRV_PIXEL_BYTES)

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    disp_init();

    uint32_t hor_res = rtk_lcd_hal_get_width();
    uint32_t ver_res = rtk_lcd_hal_get_height();
    if (hor_res != MY_DISP_HOR_RES || ver_res != MY_DISP_VER_RES)
    {
        DBG_DIRECT("lv_port_disp: panel %dx%d, buffer sized for %dx%d",
                   (int)hor_res, (int)ver_res, MY_DISP_HOR_RES, MY_DISP_VER_RES);
        while (1);
    }

    DBG_DIRECT("lv_port_disp: %dx%d, buf 0x%x/0x%x stride %d",
               (int)hor_res, (int)ver_res, (unsigned)LV_PORT_BUF1, (unsigned)LV_PORT_BUF2,
               MY_DISP_BUF_SIZE);

    lv_display_t *disp = lv_display_create(hor_res, ver_res);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_buffers(disp, LV_PORT_BUF1, LV_PORT_BUF2, MY_DISP_BUF_SIZE,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void disp_init(void)
{
    /* the panel is inited by hw_lcd_init() before the lvgl task starts */
}

volatile bool disp_flush_enabled = true;

void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    LV_UNUSED(area);

    if (disp_flush_enabled && lv_display_flush_is_last(disp_drv))
    {

        /* RGB: set_window is a no-op, update_framebuffer swaps the scanout address
         * QSPI: set_window sets the GRAM window, update_framebuffer pushes the data */
        rtk_lcd_hal_set_window(0, 0, MY_DISP_HOR_RES, MY_DISP_VER_RES);
        rtk_lcd_hal_update_framebuffer(px_map, MY_DISP_HOR_RES * MY_DISP_VER_RES);
    }

    lv_display_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
