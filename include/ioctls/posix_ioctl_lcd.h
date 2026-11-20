#ifndef POSIX_IOCTL_LCD_H
#define POSIX_IOCTL_LCD_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* ================================================================
 * LCD abstraction interface
 *
 * === write semantics (important) ===
 *   posix_write(fd, buf, count)'s count unit is [bytes], consistent with
 *   other devices such as UART/SPI/I2C, hiding bpp differences.
 *   The port layer converts it by the current cfg.bpp into the hardware transfer unit (e.g. RGB565 → /2).
 *
 *   Typical usage:
 *     uint16_t fb[W * H];
 *     posix_write(fd, fb, sizeof(fb));         // or W * H * 2
 *
 *   Row-by-row write:
 *     posix_ioctl(fd, POSIX_LCD_IOCTL_SET_WINDOW, &row_rect);
 *     posix_write(fd, line_buf, line_w * sizeof(uint16_t));
 *
 *   count must be divisible by (cfg.bpp / 8), otherwise POSIX_ERR_INVAL is returned.
 *
 * === window semantics ===
 *   posix_write internally uses the rect [recorded by the last SET_WINDOW] as the target
 *   rectangle and does not automatically reset to (0,0). Row-by-row writes must SET_WINDOW every row.
 * ================================================================ */

/* LCD interface types */
#define POSIX_LCD_IF_SPI     0   /* SPI serial */
#define POSIX_LCD_IF_8080    1   /* 8080 parallel */
#define POSIX_LCD_IF_RGB     2   /* RGB interface */
#define POSIX_LCD_IF_MIPI    3   /* MIPI DSI */

/* Config struct */
typedef struct
{
    uint16_t width;             /* resolution width */
    uint16_t height;            /* resolution height */
    uint8_t  bpp;               /* 16/18/24 */
    uint8_t  interface;         /* SPI/8080/RGB/MIPI */
} posix_lcd_config_t;

/* Window clipping */
typedef struct
{
    uint16_t x, y;
    uint16_t w, h;
} posix_lcd_rect_t;

/* ioctl commands */
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
