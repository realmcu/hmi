#ifndef POSIX_IOCTL_LCD_H
#define POSIX_IOCTL_LCD_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* LCD 接口类型 */
#define POSIX_LCD_IF_SPI     0   /* SPI 串行 */
#define POSIX_LCD_IF_8080    1   /* 8080 并口 */
#define POSIX_LCD_IF_RGB     2   /* RGB 接口 */
#define POSIX_LCD_IF_MIPI    3   /* MIPI DSI */

/* 配置结构体 */
typedef struct {
    uint16_t width;             /* 分辨率宽 */
    uint16_t height;            /* 分辨率高 */
    uint8_t  bpp;               /* 16/18/24 */
    uint8_t  interface;         /* SPI/8080/RGB/MIPI */
} posix_lcd_config_t;

/* 窗口裁剪 */
typedef struct {
    uint16_t x, y;
    uint16_t w, h;
} posix_lcd_rect_t;

/* ioctl 命令 */
#define POSIX_LCD_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 1)
#define POSIX_LCD_IOCTL_GET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 2)
#define POSIX_LCD_IOCTL_SET_WINDOW     POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 3)
#define POSIX_LCD_IOCTL_DISPLAY_ON     POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 4)
#define POSIX_LCD_IOCTL_DISPLAY_OFF    POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 5)
#define POSIX_LCD_IOCTL_SET_BRIGHTNESS POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 6)
#define POSIX_LCD_IOCTL_SET_BACKLIGHT  POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 7)
#define POSIX_LCD_IOCTL_ROTATE         POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 8)
#define POSIX_LCD_IOCTL_FLUSH_WAIT     POSIX_IOC(POSIX_DEVICE_MAGIC_LCD, 9)

#endif /* POSIX_IOCTL_LCD_H */
