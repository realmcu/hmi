#ifndef POSIX_IOCTL_I2C_H
#define POSIX_IOCTL_I2C_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* ================================================================
 * I2C controller abstraction
 *
 * Path examples: /dev/i2c0 /dev/i2c1
 *
 * Typical usage (posix_read / posix_write are no longer used; go through ioctl):
 *   posix_fd_t bus = posix_open("/dev/i2c0");
 *   posix_i2c_config_t cfg = { .speed_hz = 400000, .addr_bits = 7 };
 *   posix_ioctl(bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg);
 *
 *   // write register: addr + reg + val
 *   posix_i2c_msg_t w = { .addr = 0x19, .reg = 0x20, .reg_len = 1,
 *                         .buf = &val, .len = 1 };
 *   posix_ioctl(bus, POSIX_I2C_IOCTL_WRITE_REG, &w);
 *
 *   // multi-register read: repeated-start
 *   posix_i2c_msg_t r = { .addr = 0x19, .reg = 0x28, .reg_len = 1,
 *                         .buf = buf, .len = 6 };
 *   posix_ioctl(bus, POSIX_I2C_IOCTL_READ_REG, &r);
 * ================================================================ */

/* Speed grades (speed field in Hz: standard/fast/fast+/high-speed) */
#define POSIX_I2C_SPEED_STANDARD   100000
#define POSIX_I2C_SPEED_FAST       400000
#define POSIX_I2C_SPEED_FAST_PLUS  1000000
#define POSIX_I2C_SPEED_HIGH       3400000

/* Addressing modes */
#define POSIX_I2C_ADDR_7BIT        7
#define POSIX_I2C_ADDR_10BIT       10

/* I2C controller config */
typedef struct
{
    uint32_t speed_hz;          /* bus frequency (Hz) */
    uint8_t  addr_bits;         /* 7 or 10 */
} posix_i2c_config_t;

/* Single register read/write message
 *   addr     : target device 7-bit slave address (no R/W bit)
 *   reg      : sub-address register number; reg_len = 0 means raw read/write (no sub-address)
 *   reg_len  : sub-address byte count 0/1/2
 *   buf      : data buffer to write or receive
 *   len      : buf byte count
 */
typedef struct
{
    uint16_t addr;
    uint16_t reg;
    uint8_t  reg_len;
    uint8_t  *buf;
    size_t   len;
} posix_i2c_msg_t;

/* ioctl commands */
#define POSIX_I2C_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 1)
#define POSIX_I2C_IOCTL_GET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 2)
#define POSIX_I2C_IOCTL_WRITE_REG      POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 3)
#define POSIX_I2C_IOCTL_READ_REG       POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 4)
#define POSIX_I2C_IOCTL_RAW_WRITE      POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 5)
#define POSIX_I2C_IOCTL_RAW_READ       POSIX_IOC(POSIX_DEVICE_MAGIC_I2C, 6)

#endif /* POSIX_IOCTL_I2C_H */
