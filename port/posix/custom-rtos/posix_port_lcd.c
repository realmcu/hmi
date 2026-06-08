#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_lcd.h"
#include <string.h>
#include <stdbool.h>

#include "rtl_lcdc.h"
#include "lcd_sh8601z_410_502_qspi.h"
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Panel geometry                                                       */
/* ------------------------------------------------------------------ */

#define SH8601Z_WIDTH   410
#define SH8601Z_HEIGHT  502
#define SH8601Z_BPP     16   /* RGB565 */

/* ------------------------------------------------------------------ */
/* Private types                                                        */
/* ------------------------------------------------------------------ */

typedef struct
{
    int      unit;
    uint16_t w;
    uint16_t h;
    uint8_t  bpp;
} lcd_drv_t;

typedef struct
{
    bool               in_use;
    lcd_drv_t         *drv;
    posix_lcd_config_t cfg;
    posix_lcd_rect_t   window;
} lcd_file_t;

/* ------------------------------------------------------------------ */
/* Static pool                                                          */
/* ------------------------------------------------------------------ */

#define MAX_LCD_FILES  2
static lcd_file_t s_lcd_files[MAX_LCD_FILES];

static lcd_file_t *lcd_file_alloc(void)
{
    for (int i = 0; i < MAX_LCD_FILES; i++)
    {
        if (!s_lcd_files[i].in_use)
        {
            memset(&s_lcd_files[i], 0, sizeof(lcd_file_t));
            s_lcd_files[i].in_use = true;
            return &s_lcd_files[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Driver ops                                                           */
/* ------------------------------------------------------------------ */

static void *lcd_open(void *drv_data, const char *path)
{
    (void)path;
    lcd_drv_t  *drv  = (lcd_drv_t *)drv_data;
    lcd_file_t *file = lcd_file_alloc();
    if (!file) { return POSIX_OPEN_ERR; }

    file->drv           = drv;
    file->cfg.width     = drv->w;
    file->cfg.height    = drv->h;
    file->cfg.bpp       = drv->bpp;
    file->cfg.interface = POSIX_LCD_IF_SPI;

    file->window.x = 0;
    file->window.y = 0;
    file->window.w = drv->w;
    file->window.h = drv->h;

    rtk_lcd_hal_init();
    rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);
    return file;
}

static int lcd_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    lcd_file_t *file = (lcd_file_t *)file_priv;
    if (file) { file->in_use = false; }
    return POSIX_OK;
}

static posix_ssize_t lcd_write(void *drv_data, void *file_priv,
                               const void *buf, size_t len)
{
    (void)drv_data;
    lcd_file_t *file = (lcd_file_t *)file_priv;
    if (!buf || len == 0) { return 0; }

    rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);

    rtk_lcd_hal_start_transfer((void *)buf, (uint32_t)len);
    rtk_lcd_hal_transfer_done();

    return (posix_ssize_t)len;
}

static posix_ssize_t lcd_read(void *drv_data, void *file_priv,
                              void *buf, size_t count)
{
    (void)drv_data; (void)file_priv; (void)buf; (void)count;
    return POSIX_ERR_NOSUPP;
}

static int lcd_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    (void)drv_data;
    lcd_file_t *file = (lcd_file_t *)file_priv;

    switch (cmd)
    {
    case POSIX_LCD_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            file->cfg = *(posix_lcd_config_t *)arg;
            file->drv->w   = file->cfg.width;
            file->drv->h   = file->cfg.height;
            file->drv->bpp = file->cfg.bpp;
            file->window.x = 0; file->window.y = 0;
            file->window.w = file->cfg.width;
            file->window.h = file->cfg.height;
            return POSIX_OK;
        }

    case POSIX_LCD_IOCTL_GET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            *(posix_lcd_config_t *)arg = file->cfg;
            return POSIX_OK;
        }

    case POSIX_LCD_IOCTL_SET_WINDOW:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_lcd_rect_t *r = (posix_lcd_rect_t *)arg;
            file->window = *r;
            rtk_lcd_hal_set_window(r->x, r->y, r->w, r->h);
            return POSIX_OK;
        }

    case POSIX_LCD_IOCTL_DISPLAY_ON:
        return POSIX_OK;

    case POSIX_LCD_IOCTL_DISPLAY_OFF:
        return POSIX_OK;

    case POSIX_LCD_IOCTL_SET_BRIGHTNESS:
    case POSIX_LCD_IOCTL_SET_BACKLIGHT:
        /* 背光由 PWM 驱动负责，此处不处理 */
        return POSIX_OK;

    case POSIX_LCD_IOCTL_FLUSH_WAIT:
        rtk_lcd_hal_transfer_done();
        return POSIX_OK;

    case POSIX_LCD_IOCTL_ROTATE:
        /* TODO: 通过 QSPI 发 MADCTL 命令 */
        return POSIX_OK;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

static const posix_driver_ops_t g_lcd_ops =
{
    .open  = lcd_open,
    .close = lcd_close,
    .read  = lcd_read,
    .write = lcd_write,
    .ioctl = lcd_ioctl,
};

static lcd_drv_t s_lcd0 =
{
    .unit = 0,
    .w    = SH8601Z_WIDTH,
    .h    = SH8601Z_HEIGHT,
    .bpp  = SH8601Z_BPP,
};

static int lcd_init(void)
{
    return posix_device_register("/dev/lcd0", &g_lcd_ops, &s_lcd0);
}
POSIX_INIT_DEVICE_EXPORT(lcd_init);
