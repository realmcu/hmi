#ifndef POSIX_IOCTL_GSENSOR_H
#define POSIX_IOCTL_GSENSOR_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* 加速度量程 */
#define POSIX_GSENSOR_RANGE_2G   0
#define POSIX_GSENSOR_RANGE_4G   1
#define POSIX_GSENSOR_RANGE_8G   2
#define POSIX_GSENSOR_RANGE_16G  3

/* 三轴数据 */
typedef struct {
    int32_t x;                  /* 通常为 mg（千分之一 g） */
    int32_t y;
    int32_t z;
} posix_gsensor_axis_t;

/* 配置结构体 */
typedef struct {
    uint8_t  range;             /* 量程 2G/4G/8G/16G */
    uint8_t  odr_hz;            /* 输出数据率(Hz) */
    uint8_t  low_power;         /* 0=normal, 1=low power */
} posix_gsensor_config_t;

/* ioctl 命令 */
#define POSIX_GSENSOR_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 1)
#define POSIX_GSENSOR_IOCTL_GET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 2)
#define POSIX_GSENSOR_IOCTL_SET_POWER      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 3)
#define POSIX_GSENSOR_IOCTL_SELF_TEST      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 4)
#define POSIX_GSENSOR_IOCTL_READ_TEMP      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 5)

#endif /* POSIX_IOCTL_GSENSOR_H */
