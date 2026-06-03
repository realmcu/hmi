/**
 * @file posix_port_lcd.c
 * @brief POSIX LCD driver port for RTK8773G + SH8601Z (410x502, QSPI)
 *        Zephyr backend implementation
 */

#include "posix.h"
#include "posix_init.h"
#include <string.h>
#include <stdbool.h>

#include "ioctls/posix_ioctl_lcd.h"
#include "rtl_lcdc.h"
#include "lcd_sh8601z_410_502_qspi.h"

/* ------------------------------------------------------------------ */
/* Private types                                                        */
/* ------------------------------------------------------------------ */

#define MAX_LCD_FILES   2

/* SH8601Z panel geometry */
#define SH8601Z_WIDTH   410
#define SH8601Z_HEIGHT  502
#define SH8601Z_BPP     16   /* RGB565 default */

/**
 * @brief Per-device (driver-level) data.
 *        The RTK HAL abstracts register access, so we only need
 *        the unit index and the default panel geometry.
 */
typedef struct
{
    int      unit;
    uint16_t w;
    uint16_t h;
    uint8_t  bpp;
} lcd_drv_t;

/**
 * @brief Per-open-file state.
 *        Tracks the active window and the runtime config.
 */
typedef struct
{
    bool             in_use;
    lcd_drv_t       *drv;
    posix_lcd_config_t cfg;
    posix_lcd_rect_t   window;  /* current SET_WINDOW rect */
} lcd_file_t;

/* ------------------------------------------------------------------ */
/* Static storage                                                       */
/* ------------------------------------------------------------------ */

static lcd_file_t s_file_pool[MAX_LCD_FILES];

/* ------------------------------------------------------------------ */
/* Helper: allocate / free from static pool                            */
/* ------------------------------------------------------------------ */

static lcd_file_t *lcd_file_alloc(void)
{
    for (int i = 0; i < MAX_LCD_FILES; i++)
    {
        if (!s_file_pool[i].in_use)
        {
            memset(&s_file_pool[i], 0, sizeof(lcd_file_t));
            s_file_pool[i].in_use = true;
            return &s_file_pool[i];
        }
    }
    return NULL;
}

static void lcd_file_free(lcd_file_t *f)
{
    if (f)
    {
        f->in_use = false;
    }
}

/* ------------------------------------------------------------------ */
/* Driver ops                                                           */
/* ------------------------------------------------------------------ */

static void *lcd_open(void *drv_data, const char *path)
{
    (void)path;

    lcd_drv_t  *drv  = (lcd_drv_t *)drv_data;
    lcd_file_t *file = lcd_file_alloc();
    if (!file)
    {
        return NULL;
    }

    file->drv             = drv;
    file->cfg.width       = drv->w;
    file->cfg.height      = drv->h;
    file->cfg.bpp         = drv->bpp;
    file->cfg.interface   = POSIX_LCD_IF_SPI;   /* QSPI is SPI-family */

    /* Default window = full panel */
    file->window.x = 0;
    file->window.y = 0;
    file->window.w = drv->w;
    file->window.h = drv->h;

    /* Disable tearing-effect sync by default */
    rtk_lcd_hal_set_TE_type(LCDC_TE_TYPE_NO_TE);

    return file;
}

static int lcd_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    lcd_file_free((lcd_file_t *)file_priv);
    return 0;
}

/**
 * @brief Write pixel data to the LCD.
 *
 * Two calling conventions are supported:
 *
 *   a) buf points to a posix_lcd_write_t that carries {x,y,w,h,data,len}
 *      — if posix_lcd_write_t is defined in the ioctl header.
 *
 *   b) buf is a raw pixel buffer whose byte length equals
 *      (cfg.width * cfg.height * cfg.bpp / 8), i.e. a full-frame blit.
 *      The current SET_WINDOW rect is used in this case.
 *
 * Because the ioctl header provided does NOT define posix_lcd_write_t,
 * we always use convention (b): raw buffer + current window.
 */
static int lcd_write(void *drv_data, void *file_priv,
                     const void *buf, size_t len)
{
    (void)drv_data;
    lcd_file_t *file = (lcd_file_t *)file_priv;

    if (!buf || len == 0)
    {
        return 0;
    }

    /* Apply the window that was last set via SET_WINDOW ioctl */
    rtk_lcd_hal_set_window(file->window.x, file->window.y,
                           file->window.w, file->window.h);

    /* Kick off the transfer and wait for completion */
    rtk_lcd_hal_start_transfer((void *)buf, (uint32_t)len);
    rtk_lcd_hal_transfer_done();

    return (int)len;
}

static int lcd_read(void *drv_data, void *file_priv, void *buf, size_t count)
{
    (void)drv_data;
    (void)file_priv;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

static int lcd_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    (void)drv_data;
    lcd_file_t *file = (lcd_file_t *)file_priv;

    switch (cmd)
    {

    /* ---- configuration ---- */
    case POSIX_LCD_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            file->cfg = *(posix_lcd_config_t *)arg;
            /* Update driver's cached geometry to match new config */
            file->drv->w   = file->cfg.width;
            file->drv->h   = file->cfg.height;
            file->drv->bpp = file->cfg.bpp;
            /* Reset window to full screen on reconfigure */
            file->window.x = 0;
            file->window.y = 0;
            file->window.w = file->cfg.width;
            file->window.h = file->cfg.height;
            return 0;
        }

    case POSIX_LCD_IOCTL_GET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            *(posix_lcd_config_t *)arg = file->cfg;
            return 0;
        }

    /* ---- window ---- */
    case POSIX_LCD_IOCTL_SET_WINDOW:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_lcd_rect_t *r = (posix_lcd_rect_t *)arg;
            file->window = *r;
            rtk_lcd_hal_set_window(r->x, r->y, r->w, r->h);
            return 0;
        }

    /* ---- display on/off (stub — backlight/power managed elsewhere) ---- */
    case POSIX_LCD_IOCTL_DISPLAY_ON:
        /* TODO: assert display-enable GPIO / send DISPON command */
        return 0;

    case POSIX_LCD_IOCTL_DISPLAY_OFF:
        /* TODO: de-assert display-enable GPIO / send DISPOFF command */
        return 0;

    /* ---- brightness (stub — PWM backlight driver handles this) ---- */
    case POSIX_LCD_IOCTL_SET_BRIGHTNESS:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            int brightness = *(int *)arg;
            (void)brightness;
            /* TODO: forward to PWM backlight driver */
            return 0;
        }

    case POSIX_LCD_IOCTL_SET_BACKLIGHT:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            int level = *(int *)arg;
            (void)level;
            /* TODO: forward to PWM backlight driver */
            return 0;
        }

    /* ---- flush-wait: block until the last DMA transfer finishes ---- */
    case POSIX_LCD_IOCTL_FLUSH_WAIT:
        rtk_lcd_hal_transfer_done();
        return 0;

    /* ---- rotation (stub) ---- */
    case POSIX_LCD_IOCTL_ROTATE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            int angle = *(int *)arg;
            (void)angle;
            /* TODO: send MADCTL command via QSPI */
            return 0;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ------------------------------------------------------------------ */
/* Driver registration                                                  */
/* ------------------------------------------------------------------ */

static const posix_driver_ops_t g_lcd_ops =
{
    .open  = lcd_open,
    .close = lcd_close,
    .read  = lcd_read,
    .write = lcd_write,
    .ioctl = lcd_ioctl,
};

/* SH8601Z: 410 x 502, RGB565 (16 bpp), QSPI */
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
