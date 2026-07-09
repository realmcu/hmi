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
typedef struct
{
    int32_t x;                  /* 通常为 mg（千分之一 g） */
    int32_t y;
    int32_t z;
} posix_gsensor_axis_t;

/* 配置结构体 */
typedef struct
{
    uint8_t  range;             /* 量程 2G/4G/8G/16G */
    uint8_t  odr_hz;            /* 输出数据率(Hz) */
    uint8_t  low_power;         /* 0=normal, 1=low power */
    uint8_t  use_irq;           /* 1=启用 DRDY 中断，posix_read 阻塞等中断 */
} posix_gsensor_config_t;

/* ioctl 命令 */
#define POSIX_GSENSOR_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 1)
#define POSIX_GSENSOR_IOCTL_GET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 2)
#define POSIX_GSENSOR_IOCTL_SET_POWER      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 3)
#define POSIX_GSENSOR_IOCTL_SELF_TEST      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 4)
#define POSIX_GSENSOR_IOCTL_READ_TEMP      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 5)
/* 设置中断等待超时(ms)；arg 为 uint32_t* */
#define POSIX_GSENSOR_IOCTL_SET_TIMEOUT    POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 6)

/* ================================================================
 * Gsensor 绑定 API —— 与 touch 层对称，机制上都是"设备表别名"
 *
 * 芯片驱动 .c 各自 POSIX_INIT_DEVICE_EXPORT 注册芯片型号路径
 * （/dev/sc7a20，未来 /dev/bma253、/dev/lis3dh …）。board port 在启动
 * 阶段调 posix_gsensor_bind() 把当前板对应芯片挂成 /dev/gsensor0。
 * 上层应用只 open /dev/gsensor0；调试可直接 open /dev/sc7a20。
 *
 * 实现是框架层 posix_device_bind_alias 的类型化薄壳
 * （见 posix_device.h）。
 * ================================================================ */
int posix_gsensor_bind(const char *alias, const char *chip_path);
int posix_gsensor_unbind(const char *alias);

#endif /* POSIX_IOCTL_GSENSOR_H */
