#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_lcd.h"

typedef struct { int unit; uintptr_t reg_base; uint16_t w, h; } lcd_drv_t;
typedef struct { lcd_drv_t *drv; posix_lcd_config_t cfg; } lcd_file_t;

static void *lcd_open(void *d, const char *p)
{
    (void)p; lcd_file_t *f = (lcd_file_t *)/*alloc*/;
    lcd_drv_t *drv = (lcd_drv_t *)d;
    f->drv = drv; f->cfg.width = drv->w; f->cfg.height = drv->h; return f;
}
static int lcd_close(void *d, void *f) { (void)d; /*free f*/; return 0; }

/* posix_write = 发像素数据（刷屏） */
static int lcd_write(void *d, void *f, const void *buf, size_t len)
{
    (void)d; lcd_file_t *file = (lcd_file_t *)f;
    /* hw_lcd_send_data(file->drv->reg_base, buf, len); */
    (void)file; (void)buf; (void)len; return (int)len;
}
static int lcd_read(void *d, void *f, void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP;
}

static int lcd_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    lcd_drv_t *drv = (lcd_drv_t *)d; lcd_file_t *file = (lcd_file_t *)f;
    switch (cmd)
    {
    case POSIX_LCD_IOCTL_SET_CONFIG:
        file->cfg = *(posix_lcd_config_t *)arg;
        /* hw_lcd_init(drv->reg_base, file->cfg.width, file->cfg.height, file->cfg.bpp); */
        return 0;
    case POSIX_LCD_IOCTL_SET_WINDOW:
        {
            posix_lcd_rect_t *r = (posix_lcd_rect_t *)arg;
            /* hw_lcd_set_window(drv->reg_base, r->x, r->y, r->w, r->h); */
            (void)r; return 0;
        }
    case POSIX_LCD_IOCTL_DISPLAY_ON:  /* hw_lcd_display(drv->reg_base, 1); */ return 0;
    case POSIX_LCD_IOCTL_DISPLAY_OFF: /* hw_lcd_display(drv->reg_base, 0); */ return 0;
    case POSIX_LCD_IOCTL_SET_BRIGHTNESS:
        {
            int v = *(int *)arg; /* hw_lcd_set_brightness(drv->reg_base, v); */ (void)v; return 0;
        }
    default: return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_lcd_ops =
{
    .open = lcd_open, .close = lcd_close, .read = lcd_read, .write = lcd_write, .ioctl = lcd_ioctl,
};
static lcd_drv_t s_lcd0 = { .unit = 0, .w = 320, .h = 480 };

/* ---------- 自动注册 ---------- */
static int lcd_init(void)
{
    return posix_device_register("/dev/lcd0", &g_lcd_ops, &s_lcd0);
}
POSIX_INIT_DEVICE_EXPORT(lcd_init);
