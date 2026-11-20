/* ================================================================
 * UART driver — a posix-device adaptation layer on top of the Zephyr UART API
 *
 * Path format: /dev/uart0  → uart0 node
 *
 * Dependency: the uart0 node is enabled in the DTS, and CONFIG_UART_INTERRUPT_DRIVEN=y.
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_uart.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

/* ---------- Driver private data (one per posix device) ---------- */
typedef struct
{
    const struct device *dev;   /* Zephyr UART device */
} uart_drv_data_t;

/* ---------- per-open file private data ---------- */
typedef struct
{
    bool                in_use;
    uart_drv_data_t    *drv;
    posix_fd_t          fd;
    posix_uart_config_t cfg;
    posix_uart_rx_cb_t  rx_cb;
} uart_file_t;

/* ---------- Static file pool ---------- */
#define MAX_UART_FILES  4
static uart_file_t s_uart_files[MAX_UART_FILES];

/* ---------- Zephyr UART IRQ dispatcher ---------- */
static void uart_zephyr_irq_handler(const struct device *dev, void *user_data)
{
    uart_file_t *f = (uart_file_t *)user_data;

    while (uart_irq_update(dev) && uart_irq_rx_ready(dev))
    {
        uint8_t byte;
        int     n = uart_fifo_read(dev, &byte, 1);
        if (n == 1 && f->rx_cb.callback)
        {
            f->rx_cb.callback(f->fd, byte, f->rx_cb.arg);
        }
    }
}

/* ---------- open ---------- */
static void *uart_open(void *drv_data, const char *path)
{
    (void)path;

    uart_drv_data_t *d = (uart_drv_data_t *)drv_data;

    if (!device_is_ready(d->dev)) { return POSIX_OPEN_ERR; }

    uart_file_t *f = NULL;
    for (int i = 0; i < MAX_UART_FILES; i++)
    {
        if (!s_uart_files[i].in_use)
        {
            f = &s_uart_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use = true;
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv              = d;
    f->fd               = POSIX_FD_NULL;   /* filled in by the posix layer after open returns */
    f->cfg.baudrate     = 115200;
    f->cfg.data_bits    = 8;
    f->cfg.parity       = 0;
    f->cfg.stop_bits    = 1;
    f->cfg.flow_control = 0;
    return f;
}

/* ---------- close ---------- */
static int uart_close(void *drv_data, void *file_priv)
{
    (void)drv_data;

    uart_file_t *f = (uart_file_t *)file_priv;
    if (!f || !f->in_use) { return POSIX_OK; }

    /* If an RX interrupt callback was registered, disable it first */
    if (f->rx_cb.callback)
    {
        uart_irq_rx_disable(f->drv->dev);
    }
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read: non-blocking polled read ---------- */
static posix_ssize_t uart_read(void *drv_data, void *file_priv,
                               void *buf, size_t count)
{
    (void)drv_data;

    uart_file_t *f   = (uart_file_t *)file_priv;
    uint8_t     *dst = (uint8_t *)buf;
    size_t       got = 0;

    while (got < count)
    {
        unsigned char ch;
        int ret = uart_poll_in(f->drv->dev, &ch);
        if (ret < 0)
        {
            break;   /* no data */
        }
        dst[got++] = ch;
    }

    if (got == 0) { return POSIX_ERR_AGAIN; }
    return (posix_ssize_t)got;
}

/* ---------- write: polled transmit ---------- */
static posix_ssize_t uart_write(void *drv_data, void *file_priv,
                                const void *buf, size_t count)
{
    (void)drv_data;

    uart_file_t    *f   = (uart_file_t *)file_priv;
    const uint8_t  *src = (const uint8_t *)buf;

    for (size_t i = 0; i < count; i++)
    {
        uart_poll_out(f->drv->dev, src[i]);
    }
    return (posix_ssize_t)count;
}

/* ---------- ioctl ---------- */
static int uart_ioctl(void *drv_data, void *file_priv,
                      unsigned long cmd, void *arg)
{
    (void)drv_data;

    uart_file_t *f = (uart_file_t *)file_priv;

    switch (cmd)
    {
    case POSIX_UART_IOCTL_GET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            *(posix_uart_config_t *)arg = f->cfg;
            return POSIX_OK;
        }

    case POSIX_UART_IOCTL_SET_CONFIG:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }

            const posix_uart_config_t *cfg = (const posix_uart_config_t *)arg;

            /* Map to Zephyr uart_config */
            struct uart_config zcfg;
            zcfg.baudrate = cfg->baudrate;

            switch (cfg->parity)
            {
            case 1:  zcfg.parity = UART_CFG_PARITY_ODD;  break;
            case 2:  zcfg.parity = UART_CFG_PARITY_EVEN; break;
            default: zcfg.parity = UART_CFG_PARITY_NONE; break;
            }

            switch (cfg->stop_bits)
            {
            case 2:  zcfg.stop_bits = UART_CFG_STOP_BITS_2; break;
            default: zcfg.stop_bits = UART_CFG_STOP_BITS_1; break;
            }

            switch (cfg->data_bits)
            {
            case 5:  zcfg.data_bits = UART_CFG_DATA_BITS_5; break;
            case 6:  zcfg.data_bits = UART_CFG_DATA_BITS_6; break;
            case 7:  zcfg.data_bits = UART_CFG_DATA_BITS_7; break;
            default: zcfg.data_bits = UART_CFG_DATA_BITS_8; break;
            }

            switch (cfg->flow_control)
            {
            case 1:  zcfg.flow_ctrl = UART_CFG_FLOW_CTRL_RTS_CTS; break;
            default: zcfg.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;    break;
            }

            int ret = uart_configure(f->drv->dev, &zcfg);
            if (ret < 0)
            {
                /* If the driver does not support runtime config, just keep a software copy */
                f->cfg = *cfg;
                return POSIX_OK;
            }
            f->cfg = *cfg;
            return POSIX_OK;
        }

    case POSIX_UART_IOCTL_SET_RX_CB:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }

            /* Disable the old callback first */
            if (f->rx_cb.callback)
            {
                uart_irq_rx_disable(f->drv->dev);
            }

            if (!arg)
            {
                memset(&f->rx_cb, 0, sizeof(f->rx_cb));
                return POSIX_OK;
            }

            const posix_uart_rx_cb_t *cb = (const posix_uart_rx_cb_t *)arg;
            f->rx_cb = *cb;

            if (f->rx_cb.callback)
            {
                uart_irq_callback_user_data_set(f->drv->dev,
                                                uart_zephyr_irq_handler, f);
                uart_irq_rx_enable(f->drv->dev);
            }
            return POSIX_OK;
        }

    case POSIX_UART_IOCTL_TX_FLUSH:
        /* Zephyr has no dedicated TX flush API; return directly */
        return POSIX_OK;

    case POSIX_UART_IOCTL_RX_FLUSH:
        {
            unsigned char ch;
            while (uart_poll_in(f->drv->dev, &ch) == 0) { /* drain */ }
            return POSIX_OK;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- Driver ops table ---------- */
static const posix_driver_ops_t g_uart_ops =
{
    .open  = uart_open,
    .close = uart_close,
    .read  = uart_read,
    .write = uart_write,
    .ioctl = uart_ioctl,
};

/* ---------- Device instance ----------
 * uart2 = console/shell uart, already enabled in the overlay.
 * uart0 is disabled and not registered.
 * The posix path /dev/uart0 maps to hardware uart2 (transparent to the app).
 * ---------------------------------------------------- */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart2), okay)
static uart_drv_data_t s_uart0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(uart2)) };

/* ---------- Auto-registration ---------- */
static int uart_init(void)
{
    return posix_device_register("/dev/uart0", &g_uart_ops, &s_uart0);
}
POSIX_INIT_DEVICE_EXPORT(uart_init);
#endif
