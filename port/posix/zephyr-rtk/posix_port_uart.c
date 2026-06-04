/* ================================================================
 * posix_port_uart.c — RTK8773G UART driver (RTK SDK)
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_uart.h"
#include "rtl876x_uart.h"
#include <string.h>

/* RTK UART hardware instances */
#define UART0_DEV  ((void *)0x40000000UL)
#define UART1_DEV  ((void *)0x40001000UL)

/* RTK UART flag */
#ifndef UART_FLAG_RX_DATA_RDY
#define UART_FLAG_RX_DATA_RDY  BIT(0)
#endif

/* UART_SendData is the RTK HAL function declared in rtl876x_uart.h */

/* ---------- driver private data (one per UART controller) ---------- */
typedef struct
{
    int   unit;      /* 0 or 1 */
    void *uart_dev;  /* RTK UART device pointer */
} uart_drv_data_t;

/* ---------- per-open private data ---------- */
typedef struct
{
    uart_drv_data_t    *drv;
    posix_uart_config_t cfg;
    posix_uart_rx_cb_t  rx_cb;
} uart_file_t;

/* ---------- static file pool ---------- */
#define MAX_UART_FILES 4
static uart_file_t s_uart_files[MAX_UART_FILES];
static int         s_uart_file_used[MAX_UART_FILES];

/* ---------- open ---------- */
static void *uart_open(void *drv_data, const char *path)
{
    (void)path;

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
    if (!f)
    {
        return POSIX_OPEN_ERR;
    }

    f->drv             = (uart_drv_data_t *)drv_data;
    f->cfg.baudrate    = 115200;
    f->cfg.data_bits   = 8;
    f->cfg.parity      = 0;
    f->cfg.stop_bits   = 1;
    f->cfg.flow_control = 0;
    memset(&f->rx_cb, 0, sizeof(f->rx_cb));
    return f;
}

/* ---------- close ---------- */
static int uart_close(void *drv_data, void *file_priv)
{
    (void)drv_data;

    uart_file_t *f   = (uart_file_t *)file_priv;
    int          idx = (int)(f - s_uart_files);
    if (idx >= 0 && idx < MAX_UART_FILES)
    {
        s_uart_file_used[idx] = 0;
    }
    return POSIX_OK;
}

/* ---------- read ---------- */
static posix_ssize_t uart_read(void *drv_data, void *file_priv,
                               void *buf, size_t count)
{
    (void)file_priv;

    uart_drv_data_t *d    = (uart_drv_data_t *)drv_data;
    uint8_t         *dst  = (uint8_t *)buf;
    size_t           read = 0;

    if (posix_port_in_isr())
    {
        /* ISR context: non-blocking single byte */
        if (UART_GetFlagState(((UART_TypeDef *)d->uart_dev), UART_FLAG_RX_DATA_RDY))
        {
            dst[0] = UART_ReceiveByte(((UART_TypeDef *)d->uart_dev));
            return (posix_ssize_t)1;
        }
        return POSIX_ERR_AGAIN;
    }

    /* Task context: poll up to count bytes */
    while (read < count)
    {
        if (UART_GetFlagState(((UART_TypeDef *)d->uart_dev), UART_FLAG_RX_DATA_RDY))
        {
            dst[read++] = UART_ReceiveByte(((UART_TypeDef *)d->uart_dev));
        }
        else
        {
            break;
        }
    }

    return (posix_ssize_t)read;
}

/* ---------- write ---------- */
static posix_ssize_t uart_write(void *drv_data, void *file_priv,
                                const void *buf, size_t count)
{
    (void)file_priv;

    uart_drv_data_t *d = (uart_drv_data_t *)drv_data;

    UART_SendData(((UART_TypeDef *)d->uart_dev), (const uint8_t *)buf, (uint16_t)count);
    return (posix_ssize_t)count;
}

/* ---------- ioctl ---------- */
static int uart_ioctl(void *drv_data, void *file_priv,
                      unsigned long cmd, void *arg)
{
    uart_file_t     *f = (uart_file_t *)file_priv;
    uart_drv_data_t *d = (uart_drv_data_t *)drv_data;
    (void)d;

    switch (cmd)
    {
    case POSIX_UART_IOCTL_SET_CONFIG:
        {
            if (posix_port_in_isr())
            {
                return POSIX_ERR_ISR;
            }
            const posix_uart_config_t *cfg = (const posix_uart_config_t *)arg;
            /* TODO: configure RTK UART hardware via RTK SDK
             * e.g. UART_InitTypeDef init; init.baudrate = cfg->baudrate; ...
             * UART_Init(((UART_TypeDef *)d->uart_dev), &init); */
            f->cfg = *cfg;
            return POSIX_OK;
        }

    case POSIX_UART_IOCTL_GET_CONFIG:
        *(posix_uart_config_t *)arg = f->cfg;
        return POSIX_OK;

    case POSIX_UART_IOCTL_SET_RX_CB:
        {
            const posix_uart_rx_cb_t *cb = (const posix_uart_rx_cb_t *)arg;
            f->rx_cb = *cb;
            /* Enable RX interrupt in RTK SDK so that the ISR calls:
             *   if (f->rx_cb.callback)
             *       f->rx_cb.callback(fd, byte, f->rx_cb.arg); */
            return POSIX_OK;
        }

    case POSIX_UART_IOCTL_TX_FLUSH:
        /* TODO: wait for TX FIFO/shift register empty if RTK SDK provides API */
        return POSIX_OK;

    case POSIX_UART_IOCTL_RX_FLUSH:
        /* Drain any pending RX bytes */
        while (UART_GetFlagState(((UART_TypeDef *)d->uart_dev), UART_FLAG_RX_DATA_RDY))
        {
            (void)UART_ReceiveByte(((UART_TypeDef *)d->uart_dev));
        }
        return POSIX_OK;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- driver ops table ---------- */
const posix_driver_ops_t g_uart_ops =
{
    .open  = uart_open,
    .close = uart_close,
    .read  = uart_read,
    .write = uart_write,
    .ioctl = uart_ioctl,
};

/* ---------- device instances ---------- */
static uart_drv_data_t s_uart0 = { .unit = 0, .uart_dev = UART0_DEV };
static uart_drv_data_t s_uart1 = { .unit = 1, .uart_dev = UART1_DEV };

/* ---------- init + auto-register ---------- */
static int uart_init(void)
{
    void *privs[] = { &s_uart0, &s_uart1 };
    return posix_device_register_group("/dev/uart%d", 2,
                                       &g_uart_ops, privs);
}
POSIX_INIT_DEVICE_EXPORT(uart_init);
