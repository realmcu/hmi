#ifndef POSIX_IOCTL_LCD_H
#define POSIX_IOCTL_LCD_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* ================================================================
 * LCD 抽象接口
 *
 * === write 语义（重要） ===
 *   posix_write(fd, buf, count) 的 count 单位是【字节】，与
 *   UART/SPI/I2C 等其它设备保持一致，屏蔽 bpp 差异。
 *   port 层根据当前 cfg.bpp 折算成硬件传输单位（如 RGB565 → /2）。
 *
 *   典型用法：
 *     uint16_t fb[W * H];
 *     posix_write(fd, fb, sizeof(fb));         // 或 W * H * 2
 *
 *   逐行写：
 *     posix_ioctl(fd, POSIX_LCD_IOCTL_SET_WINDOW, &row_rect);
 *     posix_write(fd, line_buf, line_w * sizeof(uint16_t));
 *
 *   count 必须能被 (cfg.bpp / 8) 整除，否则返回 POSIX_ERR_INVAL。
 *
 * === window 语义 ===
 *   posix_write 内部使用【上一次 SET_WINDOW 记录的】rect 作为目标
 *   矩形，不会自动重置到 (0,0)。逐行写时每行都要 SET_WINDOW。
 * ================================================================ */

/* LCD 接口类型 */
#define POSIX_LCD_IF_SPI     0   /* SPI 串行 */
#define POSIX_LCD_IF_8080    1   /* 8080 并口 */
#define POSIX_LCD_IF_RGB     2   /* RGB 接口 */
#define POSIX_LCD_IF_MIPI    3   /* MIPI DSI */

/* 配置结构体 */
typedef struct
{
    uint16_t width;             /* 分辨率宽 */
    uint16_t height;            /* 分辨率高 */
    uint8_t  bpp;               /* 16/18/24 */
    uint8_t  interface;         /* SPI/8080/RGB/MIPI */
} posix_lcd_config_t;

/* 窗口裁剪 */
typedef struct
{
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
