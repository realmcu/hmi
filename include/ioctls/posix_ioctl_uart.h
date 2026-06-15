#ifndef POSIX_IOCTL_UART_H
#define POSIX_IOCTL_UART_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* UART 配置结构体 */
typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;
    uint8_t  parity;
    uint8_t  stop_bits;
    uint8_t  flow_control;
} posix_uart_config_t;

/* 接收中断回调 */
typedef struct {
    void (*callback)(posix_fd_t fd, uint8_t byte, void *arg);
    void *arg;
} posix_uart_rx_cb_t;

/* ioctl 命令 */
#define POSIX_UART_IOCTL_GET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_UART, 1)
#define POSIX_UART_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_UART, 2)
#define POSIX_UART_IOCTL_SET_RX_CB      POSIX_IOC(POSIX_DEVICE_MAGIC_UART, 3)
#define POSIX_UART_IOCTL_TX_FLUSH       POSIX_IOC(POSIX_DEVICE_MAGIC_UART, 4)
#define POSIX_UART_IOCTL_RX_FLUSH       POSIX_IOC(POSIX_DEVICE_MAGIC_UART, 5)

#endif
