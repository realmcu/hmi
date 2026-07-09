#ifndef POSIX_IOCTL_TOUCH_H
#define POSIX_IOCTL_TOUCH_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* 触摸状态 */
#define POSIX_TOUCH_IDLE    0
#define POSIX_TOUCH_PRESS   1
#define POSIX_TOUCH_RELEASE 2

/* 触摸点数据（支持多点触摸） */
typedef struct
{
    uint8_t  touch_id;          /* 触摸点 ID */
    uint8_t  status;            /* PRESS/RELEASE/IDLE */
    uint16_t x;
    uint16_t y;
    uint16_t pressure;          /* 压力值 */
} posix_touch_point_t;

/* 触摸读取结果 */
typedef struct
{
    uint8_t  point_count;       /* 有效触摸点数 */
    posix_touch_point_t points[5];  /* 最多 5 点 */
} posix_touch_data_t;

/* 配置结构体 */
typedef struct
{
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

/* ================================================================
 * Touch 绑定 API —— 让"应用只 open /dev/touch0"成为可能
 *
 * 芯片驱动 .c（如 posix_port_cst816d.c / posix_port_chsc6417.c）自身
 * 通过 POSIX_INIT_DEVICE_EXPORT 注册 /dev/cst816d、/dev/chsc6417 —— 这
 * 些路径始终可见，方便调试与直接访问。
 *
 * board port 在启动阶段调用 posix_touch_bind() 决定"当前板上哪一颗是
 * 上层认知的 /dev/touch0"。绑定的语义是【别名注册】：查已注册芯片路径
 * 的 ops+drv_data，用同一份再以 alias 名字注册到设备表。两条路径共享
 * 同一颗芯片的 ops，读取到的坐标/状态完全一致。
 *
 * 典型：
 *   posix_touch_bind("/dev/touch0", "/dev/cst816d");
 *   posix_fd_t tp = posix_open("/dev/touch0");
 *
 * 注意：
 *  - chip_path 必须已经注册（芯片驱动的 POSIX_INIT_DEVICE_EXPORT 属于
 *    优先级 1；调用 bind 的 board port init 应用 POSIX_INIT_APP_EXPORT
 *    以保证顺序）。
 *  - alias 与 chip_path 的 ref_count 独立。同时 open 两条路径会占两个
 *    fd 槽和两个芯片 file 槽，touch 场景通常只 open 其一，不必担心。
 *  - 若 alias 已存在返回 POSIX_ERR_BUSY；chip_path 未注册返回
 *    POSIX_ERR_NODEV。
 * ================================================================ */
int posix_touch_bind(const char *alias, const char *chip_path);
int posix_touch_unbind(const char *alias);

#endif /* POSIX_IOCTL_TOUCH_H */
