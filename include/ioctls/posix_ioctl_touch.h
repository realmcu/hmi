#ifndef POSIX_IOCTL_TOUCH_H
#define POSIX_IOCTL_TOUCH_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* 触摸状态 */
#define POSIX_TOUCH_IDLE    0
#define POSIX_TOUCH_PRESS   1
#define POSIX_TOUCH_RELEASE 2

/* 触摸点数据（支持多点触摸） */
typedef struct {
    uint8_t  touch_id;          /* 触摸点 ID */
    uint8_t  status;            /* PRESS/RELEASE/IDLE */
    uint16_t x;
    uint16_t y;
    uint16_t pressure;          /* 压力值 */
} posix_touch_point_t;

/* 触摸读取结果 */
typedef struct {
    uint8_t  point_count;       /* 有效触摸点数 */
    posix_touch_point_t points[5];  /* 最多 5 点 */
} posix_touch_data_t;

/* 配置结构体 */
typedef struct {
    uint8_t  i2c_addr;          /* I2C 地址 */
    uint16_t width;             /* TP 分辨率宽 */
    uint16_t height;            /* TP 分辨率高 */
    uint8_t  swap_xy;           /* 是否交换 XY */
} posix_touch_config_t;

/* ioctl 命令 */
#define POSIX_TOUCH_IOCTL_SET_CONFIG    POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 1)
#define POSIX_TOUCH_IOCTL_GET_CONFIG    POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 2)
#define POSIX_TOUCH_IOCTL_CALIBRATE     POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 3)
#define POSIX_TOUCH_IOCTL_SET_POWER     POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 4)

#endif /* POSIX_IOCTL_TOUCH_H */
