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
#if defined(CONFIG_REALTEK_LCD_SH8601Z_410_502_QSPI)
#include "lcd_sh8601z_410_502_qspi.h"
#elif defined(CONFIG_REALTEK_LCD_ST77916_360_360_QSPI)
#include "lcd_st77916_360_360_qspi.h"
#elif CONFIG_REALTEK_LCD_ST7801N_466_466_QSPI
#include "lcd_st7801n_466_466_qspi.h"
#else
#error "posix_port_lcd.c: no supported panel Kconfig enabled"
#endif

/* ------------------------------------------------------------------ */
/* Private types                                                        */
/* ------------------------------------------------------------------ */

#define MAX_LCD_FILES   2


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
        return POSIX_OPEN_ERR;
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
 * count 语义（对齐 posix_ioctl_lcd.h 顶部约定）：
 *   len 是【字节数】，与 UART/SPI/I2C 等其它设备保持一致。
 *   port 层按 file->cfg.bpp 折算成 RTK HAL 期望的像素数：
 *     RGB565 (bpp=16): pixels = len / 2
 *     RGB888 (bpp=24): pixels = len / 3
 *   len 必须能被 (bpp/8) 整除，否则返回 POSIX_ERR_INVAL。
 *
 * 目标窗口取自 file->window（上一次 SET_WINDOW 记录的 rect），
 * write 不会自动重置窗口起点。
 */
static posix_ssize_t lcd_write(void *drv_data, void *file_priv,
                               const void *buf, size_t len)
{
    (void)drv_data;
    lcd_file_t *file = (lcd_file_t *)file_priv;

    if (!buf || len == 0)
    {
        return 0;
    }

    /* 字节数 → 像素数：RTK HAL 的 rtk_lcd_hal_start_transfer 期望像素数
     * （内部 GDMA burst 用 Word=4B/2 像素，len>>1 得到 word 数），
     * 因此这里必须做单位换算，不能把字节数原样透传。 */
    uint8_t bytes_per_pixel = file->cfg.bpp / 8;
    if (bytes_per_pixel == 0 || (len % bytes_per_pixel) != 0)
    {
        return POSIX_ERR_INVAL;
    }
    uint32_t pixels = (uint32_t)(len / bytes_per_pixel);

    /* Apply the window that was last set via SET_WINDOW ioctl */
    rtk_lcd_hal_set_window(file->window.x, file->window.y,
                           file->window.w, file->window.h);

    /* Kick off the transfer and wait for completion */
    rtk_lcd_hal_start_transfer((void *)buf, pixels);
    rtk_lcd_hal_transfer_done();

    return (posix_ssize_t)len;   /* 与 count 对称，返回字节数 */
}

static posix_ssize_t lcd_read(void *drv_data, void *file_priv, void *buf, size_t count)
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
            /* drv->w/h/bpp 是共享驱动数据，不能在 per-open 的 ioctl 中修改；
             * 窗口和像素格式只更新此 fd 自己的 cfg 和 window。 */
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

    /* ---- window ----
     * 只记录到 file->window，实际写硬件窗口寄存器统一在 lcd_write 里
     * 完成。避免"SET_WINDOW 时写一次、write 前再写一次"的重复调用，
     * 也让"多次 SET_WINDOW 后只做一次刷屏"这种用法零开销。 */
    case POSIX_LCD_IOCTL_SET_WINDOW:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            file->window = *(posix_lcd_rect_t *)arg;
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


static lcd_drv_t s_lcd0 =
{
    .unit = 0,
    .w    = 0,
    .h    = 0,
    .bpp  = 0,
};

static int lcd_init(void)
{
    s_lcd0.w   = rtk_lcd_hal_get_width();
    s_lcd0.h   = rtk_lcd_hal_get_height();
    s_lcd0.bpp = rtk_lcd_hal_get_pixel_bits();
    return posix_device_register("/dev/lcd0", &g_lcd_ops, &s_lcd0);
}
POSIX_INIT_DEVICE_EXPORT(lcd_init);
