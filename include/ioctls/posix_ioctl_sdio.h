#ifndef POSIX_IOCTL_SDIO_H
#define POSIX_IOCTL_SDIO_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

#define POSIX_SDIO_BUS_1BIT  1
#define POSIX_SDIO_BUS_4BIT  4
#define POSIX_SDIO_BUS_8BIT  8

#define POSIX_SDIO_SPEED_DEFAULT  0
#define POSIX_SDIO_SPEED_HIGH     1

/* 配置结构体 */
typedef struct {
    uint8_t  bus_width;
    uint8_t  speed_mode;
    uint32_t max_freq_hz;
} posix_sdio_config_t;

/* SD 卡信息 */
typedef struct {
    uint32_t block_count;
    uint32_t block_size;        /* usually 512 */
    uint32_t capacity_kb;
    uint8_t  is_mmc;
} posix_sdio_info_t;

/* 块读写参数 */
typedef struct {
    uint32_t block_addr;
    uint32_t block_count;
    void    *data;
} posix_sdio_block_t;

/* SDIO 功能号命令 (CMD52/CMD53) */
typedef struct {
    uint8_t  func_num;          /* 0~7 */
    uint32_t addr;
    uint8_t  *data;
    size_t   len;
    uint8_t  write;             /* 0=read, 1=write */
    uint8_t  use_block;         /* 0=byte(CMD52), 1=block(CMD53) */
} posix_sdio_func_t;

/* ioctl 命令 */
#define POSIX_SDIO_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_SDIO, 1)
#define POSIX_SDIO_IOCTL_GET_INFO       POSIX_IOC(POSIX_DEVICE_MAGIC_SDIO, 2)
#define POSIX_SDIO_IOCTL_READ_BLOCKS    POSIX_IOC(POSIX_DEVICE_MAGIC_SDIO, 3)
#define POSIX_SDIO_IOCTL_WRITE_BLOCKS   POSIX_IOC(POSIX_DEVICE_MAGIC_SDIO, 4)
#define POSIX_SDIO_IOCTL_CARD_DETECT    POSIX_IOC(POSIX_DEVICE_MAGIC_SDIO, 5)
#define POSIX_SDIO_IOCTL_FUNC_CMD       POSIX_IOC(POSIX_DEVICE_MAGIC_SDIO, 6)

#endif
