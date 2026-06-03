/* ================================================================
 * UART 驱动示例 — 基于自研 RTOS 的移植模板
 *
 * 移植时替换 "your_rtos_" 为实际 RTOS 的 API
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_uart.h"
#include <string.h>

/* ---------- 设备私有数据（每个 UART 控制器一个）---------- */
/* ---------- 静态文件池 ---------- */
#define MAX_UART_FILES   4
static uart_file_t s_uart_files[MAX_UART_FILES];
static int s_uart_file_used[MAX_UART_FILES];

typedef struct
{
    int              unit;            /* 0/1/2 */
    uintptr_t        reg_base;        /* 寄存器基址 */
} uart_drv_data_t;

/* ---------- per-open 私有数据 ---------- */
typedef struct
{
    uart_drv_data_t  *drv;            /* 指向 drv_data */
    posix_uart_config_t cfg;          /* 当前配置 */
    posix_uart_rx_cb_t rx_cb;        /* 接收回调 */
} uart_file_t;

/* ---------- open ---------- */
static void *uart_open(void *drv_data, const char *path)
{
    /* 从静态池分配，避免堆碎片 */
    uart_file_t *f = NULL;
    for (int i = 0; i < MAX_UART_FILES; i++)
    {
        if (!s_uart_file_used[i])
        {
            s_uart_file_used[i] = 1;
            f = &s_uart_files[i];
            break;
        }
    }
    if (!f) { return NULL; }

    f->drv = (uart_drv_data_t *)drv_data;
    f->cfg.baudrate = 115200;  /* 默认配置 */
    f->cfg.data_bits = 8;
    f->cfg.parity = 0;
    f->cfg.stop_bits = 1;
    f->cfg.flow_control = 0;
    memset(&f->rx_cb, 0, sizeof(f->rx_cb));
    return f;
}

/* ---------- close ---------- */
static int uart_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    /* 归还到静态池 */
    uart_file_t *f = (uart_file_t *)file_priv;
    int idx = f - s_uart_files;
    if (idx >= 0 && idx < MAX_UART_FILES)
    {
        s_uart_file_used[idx] = 0;
    }
    return POSIX_OK;
}

/* ---------- read ---------- */
static int uart_read(void *drv_data, void *file_priv,
                     void *buf, size_t count)
{
    uart_file_t *f = (uart_file_t *)file_priv;
    uart_drv_data_t *d = (uart_drv_data_t *)drv_data;

    if (posix_port_in_isr())
    {
        /* ISR 中非阻塞读，有数据就读一个 */
        /* int c = hw_uart_read_char(d->reg_base); */
        /* if (c >= 0) { *(uint8_t*)buf = (uint8_t)c; return 1; } */
        return POSIX_ERR_AGAIN;
    }

    /* 任务上下文阻塞读 */
    /* return hw_uart_read(d->reg_base, buf, count, f->cfg.baudrate); */
    (void)f;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

/* ---------- write ---------- */
static int uart_write(void *drv_data, void *file_priv,
                      const void *buf, size_t count)
{
    uart_drv_data_t *d = (uart_drv_data_t *)drv_data;
    (void)file_priv;

    if (posix_port_in_isr())
    {
        /* ISR 中：逐个写入 TX FIFO */
        /* for (size_t i = 0; i < count; i++)
         *     hw_uart_write_char(d->reg_base, ((const uint8_t*)buf)[i]); */
        return (int)count;
    }

    /* 任务上下文：阻塞发送 */
    /* return hw_uart_write(d->reg_base, buf, count); */
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int uart_ioctl(void *drv_data, void *file_priv,
                      unsigned long cmd, void *arg)
{
    uart_file_t *f = (uart_file_t *)file_priv;
    uart_drv_data_t *d = (uart_drv_data_t *)drv_data;
    (void)d;

    switch (cmd)
    {
    case POSIX_UART_IOCTL_SET_CONFIG:
        {
            if (cmd & POSIX_FLAG_ISR) { return POSIX_ERR_ISR; }
            const posix_uart_config_t *cfg = (const posix_uart_config_t *)arg;
            /* hw_uart_config(d->reg_base, cfg->baudrate, cfg->data_bits,
             *     cfg->parity, cfg->stop_bits, cfg->flow_control); */
            f->cfg = *cfg;
            return POSIX_OK;
        }
    case POSIX_UART_IOCTL_GET_CONFIG:
        *(posix_uart_config_t *)arg = f->cfg;
        return POSIX_OK;

    case POSIX_UART_IOCTL_SET_RX_CB:
        {
            /* ISR 中注册回调 */
            const posix_uart_rx_cb_t *cb = (const posix_uart_rx_cb_t *)arg;
            f->rx_cb = *cb;
            /* 使能接收中断，在 ISR 中调用：
             *   if (f->rx_cb.callback) f->rx_cb.callback(fd, byte, f->rx_cb.arg); */
            return POSIX_OK;
        }
    case POSIX_UART_IOCTL_TX_FLUSH:
        /* hw_uart_wait_tx_done(d->reg_base); */
        return POSIX_OK;

    case POSIX_UART_IOCTL_RX_FLUSH:
        /* hw_uart_flush_rx(d->reg_base); */
        return POSIX_OK;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
const posix_driver_ops_t g_uart_ops =
{
    .open  = uart_open,
    .close = uart_close,
    .read  = uart_read,
    .write = uart_write,
    .ioctl = uart_ioctl,
};

/* ---------- 设备实例 + 自动注册 ---------- */
static uart_drv_data_t s_uart0 = { .unit = 0, .reg_base = 0x40001000 };
static uart_drv_data_t s_uart1 = { .unit = 1, .reg_base = 0x40002000 };

static int uart_init(void)
{
    void *privs[] = { &s_uart0, &s_uart1 };
    return posix_device_register_group("/dev/uart%d", 2,
                                       &g_uart_ops, privs);
}
POSIX_INIT_DEVICE_EXPORT(uart_init);
