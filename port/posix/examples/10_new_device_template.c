/* ================================================================
 * 新设备类型添加模板 — 以 LCD 为例
 *
 * 当你需要添加一种新外设（LCD/Touch/Gsensor/...）时：
 *
 * 步骤 1：定义 magic（在 posix_device.h 中）
 *   #define POSIX_DEVICE_MAGIC_LCD  0x07
 *
 * 步骤 2：写 ioctl 头文件 include/ioctls/posix_ioctl_lcd.h
 * 步骤 3：实现本文件中的驱动 ops
 * 步骤 4：调用 posix_device_register() 注册
 *
 * 核心框架（include/*, core/*）不需要改一行。
 * ================================================================ */

/* ---- 步骤 2：ioctl 头文件（通常放在 include/ioctls/） ---- */
#if 0  /* 这是伪代码，实际放在 posix_ioctl_lcd.h 中 */

#ifndef POSIX_IOCTL_LCD_H
#define POSIX_IOCTL_LCD_H

#include "posix_ioctl.h"
#include "posix_device.h"

/* 新设备的 magic（需要在 posix_device.h 中预先定义） */
#define POSIX_DEVICE_MAGIC_LCD  0x07

/* LCD 配置结构体 */
typedef struct
{
    uint16_t width;              /* 分辨率宽 */
    uint16_t height;             /* 分辨率高 */
    uint8_t  bpp;                /* 16/18/24 */
    uint8_t  interface;          /* 0=SPI, 1=8080, 2=RGB */
} posix_lcd_config_t;

/* 窗口设置 */
typedef struct
{
    uint16_t x, y;
    uint16_t w, h;
} posix_lcd_rect_t;

/* ioctl 命令 */
#define POSIX_LCD_IOCTL_SET_CONFIG   POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 1)
#define POSIX_LCD_IOCTL_SET_WINDOW   POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 2)
#define POSIX_LCD_IOCTL_DISPLAY_ON   POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 3)
#define POSIX_LCD_IOCTL_DISPLAY_OFF  POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 4)
#define POSIX_LCD_IOCTL_SET_BRIGHTNESS   POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 5)

#endif
#endif

/* ---- 步骤 3：驱动实现 ---- */
#include "posix.h"
/* #include "ioctls/posix_ioctl_lcd.h" */

/* 设备私有数据（一个 LCD 控制器一个） */
typedef struct
{
    int       unit;
    uintptr_t reg_base;          /* 寄存器基址或 SPI fd */
    uint16_t  width;
    uint16_t  height;
} lcd_drv_data_t;

/* per-open 私有数据 */
typedef struct
{
    lcd_drv_data_t *drv;
    posix_lcd_config_t cfg;
} lcd_file_t;

/* open */
static void *lcd_open(void *drv_data, const char *path)
{
    lcd_drv_data_t *d = (lcd_drv_data_t *)drv_data;
    lcd_file_t *f = (lcd_file_t *) /* 从内存池分配 */;
    if (!f) { return NULL; }

    f->drv = d;
    f->cfg.width  = d->width;
    f->cfg.height = d->height;
    return f;
}

static int lcd_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    /* 释放 file_priv */
    return POSIX_OK;
}

/* read = 读像素点（可选） */
static int lcd_read(void *drv_data, void *file_priv,
                    void *buf, size_t count)
{
    (void)drv_data;
    (void)file_priv;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;   /* LCD 通常不支持读 */
}

/* write = 写像素数据（帧缓冲） */
static int lcd_write(void *drv_data, void *file_priv,
                     const void *buf, size_t count)
{
    lcd_file_t *f = (lcd_file_t *)file_priv;
    (void)drv_data;
    /* 向 LCD 发送像素数据 */
    /* lcd_send_data(f->drv->reg_base, buf, count); */
    return (int)count;
}

/* ioctl = 配置/控制 */
static int lcd_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    lcd_file_t *f = (lcd_file_t *)file_priv;
    lcd_drv_data_t *d = (lcd_drv_data_t *)drv_data;
    (void)d;

    switch (cmd)
    {
    case POSIX_LCD_IOCTL_SET_CONFIG:
        f->cfg = *(posix_lcd_config_t *)arg;
        /* lcd_hw_init(d->reg_base, &f->cfg); */
        return POSIX_OK;
    case POSIX_LCD_IOCTL_SET_BRIGHTNESS:
        {
            int brightness = *(int *)arg;
            /* lcd_set_brightness(d->reg_base, brightness); */
            (void)brightness;
            return POSIX_OK;
        }
    case POSIX_LCD_IOCTL_DISPLAY_ON:
        /* lcd_display_on(d->reg_base); */
        return POSIX_OK;
    case POSIX_LCD_IOCTL_DISPLAY_OFF:
        /* lcd_display_off(d->reg_base); */
        return POSIX_OK;
    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* 驱动函数表 */
static const posix_driver_ops_t s_lcd_ops =
{
    .open  = lcd_open,
    .close = lcd_close,
    .read  = lcd_read,
    .write = lcd_write,
    .ioctl = lcd_ioctl,
};

/* ---- 步骤 4：注册 ---- */
static lcd_drv_data_t s_lcd0 =
{
    .unit = 0,
    .reg_base = 0,
    .width = 320,
    .height = 480,
};

int lcd_init(void)
{
    return posix_device_register("/dev/lcd0", &s_lcd_ops, &s_lcd0);
}

/* ---- 应用代码示例 ---- */
void app_use_lcd(void)
{
    posix_fd_t lcd = posix_open("/dev/lcd0");
    if (!lcd) { return; }

    /* 配置 */
    posix_lcd_config_t cfg = { .width = 320, .height = 480, .bpp = 16 };
    posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_CONFIG, &cfg);

    /* 刷屏：发像素数据 */
    uint16_t framebuffer[320 * 480];
    posix_write(lcd, framebuffer, sizeof(framebuffer));

    /* 调亮度 */
    posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_BRIGHTNESS, &(int) {80});

    posix_close(lcd);
}
