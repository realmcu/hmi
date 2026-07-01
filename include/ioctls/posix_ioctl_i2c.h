#ifndef POSIX_IOCTL_I2C_H
#define POSIX_IOCTL_I2C_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* ================================================================
 * I2C 控制器抽象
 *
 * 路径示例：/dev/i2c0 /dev/i2c1
 *
 * 典型用法（不再使用 posix_read / posix_write，统一走 ioctl）：
 *   posix_fd_t bus = posix_open("/dev/i2c0");
 *   posix_i2c_config_t cfg = { .speed_hz = 400000, .addr_bits = 7 };
 *   posix_ioctl(bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg);
 *
 *   // 写寄存器：addr + reg + val
 *   posix_i2c_msg_t w = { .addr = 0x19, .reg = 0x20, .reg_len = 1,
 *                         .buf = &val, .len = 1 };
 *   posix_ioctl(bus, POSIX_I2C_IOCTL_WRITE_REG, &w);
 *
 *   // 读多寄存器：repeated-start
 *   posix_i2c_msg_t r = { .addr = 0x19, .reg = 0x28, .reg_len = 1,
 *                         .buf = buf, .len = 6 };
 *   posix_ioctl(bus, POSIX_I2C_IOCTL_READ_REG, &r);
 * ================================================================ */

/* 速率档（速率字段直接写 Hz，标准/快速/快速+/高速） */
#define POSIX_I2C_SPEED_STANDARD   100000
#define POSIX_I2C_SPEED_FAST       400000
#define POSIX_I2C_SPEED_FAST_PLUS  1000000
#define POSIX_I2C_SPEED_HIGH       3400000

/* 寻址模式 */
#define POSIX_I2C_ADDR_7BIT        7
#define POSIX_I2C_ADDR_10BIT       10

/* I2C 控制器配置 */
typedef struct
{
    uint32_t speed_hz;          /* 总线频率（Hz） */
    uint8_t  addr_bits;         /* 7 或 10 */
} posix_i2c_config_t;

/* 单次寄存器读写消息
 *   addr     : 目标设备 7-bit 从地址（不含 R/W 位）
 *   reg      : 子地址寄存器号；当 reg_len = 0 表示纯 raw 读/写（无子地址）
 *   reg_len  : 子地址字节数 0/1/2
 *   buf      : 写入或接收数据缓冲
 *   len      : buf 字节数
 */
typedef struct
{
    uint16_t addr;
    uint16_t reg;
    uint8_t  reg_len;
    uint8_t  *buf;
    size_t   len;
} posix_i2c_msg_t;

/* ioctl 命令 */
#define POSIX_I2C_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 1)
#define POSIX_I2C_IOCTL_GET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 2)
#define POSIX_I2C_IOCTL_WRITE_REG      POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 3)
#define POSIX_I2C_IOCTL_READ_REG       POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 4)
#define POSIX_I2C_IOCTL_RAW_WRITE      POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 5)
#define POSIX_I2C_IOCTL_RAW_READ       POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 6)

#endif /* POSIX_IOCTL_I2C_H */
