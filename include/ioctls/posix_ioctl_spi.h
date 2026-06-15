#ifndef POSIX_IOCTL_SPI_H
#define POSIX_IOCTL_SPI_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* SPI 模式 */
#define POSIX_SPI_MODE_0  0  /* CPOL=0, CPHA=0 */
#define POSIX_SPI_MODE_1  1  /* CPOL=0, CPHA=1 */
#define POSIX_SPI_MODE_2  2  /* CPOL=1, CPHA=0 */
#define POSIX_SPI_MODE_3  3  /* CPOL=1, CPHA=1 */

/* SPI CS 极性 */
#define POSIX_SPI_CS_ACTIVE_LOW   0
#define POSIX_SPI_CS_ACTIVE_HIGH  1

/* 配置结构体 */
typedef struct {
    uint32_t freq_hz;
    uint8_t  mode;
    uint8_t  bits_per_word;
    int      cs_pin;          /* -1 = 应用层自己控制 CS */
} posix_spi_config_t;

/* 全双工传输 */
typedef struct {
    const void *tx_buf;       /* NULL = 只收不发 */
    void       *rx_buf;       /* NULL = 只发不收 */
    size_t      len;
} posix_spi_transfer_t;

/* ioctl 命令 */
#define POSIX_SPI_IOCTL_SET_CONFIG      POSIX_IOC(POSIX_DEVICE_MAGIC_SPI, 1)
#define POSIX_SPI_IOCTL_GET_CONFIG      POSIX_IOC(POSIX_DEVICE_MAGIC_SPI, 2)
#define POSIX_SPI_IOCTL_TRANSFER        POSIX_IOC(POSIX_DEVICE_MAGIC_SPI, 3)
#define POSIX_SPI_IOCTL_CS_TAKE         POSIX_IOC(POSIX_DEVICE_MAGIC_SPI, 4)
#define POSIX_SPI_IOCTL_CS_RELEASE      POSIX_IOC(POSIX_DEVICE_MAGIC_SPI, 5)

#endif
