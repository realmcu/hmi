/* ================================================================
 * SPI 驱动 — 基于 Zephyr SPI API 的 posix-device 适配层
 *
 * 路径格式: /dev/spi0, /dev/spi1
 *   /dev/spi0  → spi0 节点
 *   /dev/spi1  → spi1 节点
 *
 * 依赖：DTS 中 spi0 / spi1 节点已 enabled，且 CONFIG_SPI=y。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_spi.h"

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <string.h>

/* ---------- 控制器私有数据（每个 posix 设备一份） ---------- */
typedef struct
{
    const struct device *dev;   /* Zephyr SPI device */
} spi_drv_data_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct
{
    bool              in_use;
    spi_drv_data_t   *drv;
    posix_spi_config_t cfg;
    struct spi_config  zephyr_cfg;
} spi_file_t;

#define MAX_SPI_FILES  4
static spi_file_t s_spi_files[MAX_SPI_FILES];

/* ---------- 将 posix_spi_config_t 转换为 spi_config ---------- */
static void spi_build_zephyr_cfg(const posix_spi_config_t *cfg,
                                 struct spi_config *zcfg)
{
    uint16_t operation = SPI_OP_MODE_MASTER
                         | SPI_TRANSFER_MSB
                         | SPI_WORD_SET(cfg->bits_per_word);

    if (cfg->mode & 0x02U) { operation |= SPI_MODE_CPOL; }
    if (cfg->mode & 0x01U) { operation |= SPI_MODE_CPHA; }

    zcfg->frequency = cfg->freq_hz;
    zcfg->operation = operation;
    zcfg->slave     = 0;
    zcfg->cs        = (struct spi_cs_control) { 0 };
}

/* ---------- open ---------- */
static void *spi_open(void *drv_data, const char *path)
{
    (void)path;
    spi_drv_data_t *d = (spi_drv_data_t *)drv_data;

    if (!device_is_ready(d->dev)) { return POSIX_OPEN_ERR; }

    spi_file_t *f = NULL;
    for (int i = 0; i < MAX_SPI_FILES; i++)
    {
        if (!s_spi_files[i].in_use)
        {
            f = &s_spi_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use = true;
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv               = d;
    f->cfg.freq_hz       = 1000000U;
    f->cfg.mode          = POSIX_SPI_MODE_0;
    f->cfg.bits_per_word = 8U;
    f->cfg.cs_pin        = -1;

    spi_build_zephyr_cfg(&f->cfg, &f->zephyr_cfg);
    return f;
}

/* ---------- close ---------- */
static int spi_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    spi_file_t *f = (spi_file_t *)file_priv;
    if (!f || !f->in_use) { return POSIX_OK; }
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read：发 dummy 字节，接收 MISO ---------- */
static posix_ssize_t spi_read_fn(void *drv_data, void *file_priv,
                                 void *buf, size_t len)
{
    (void)drv_data;
    spi_file_t *f = (spi_file_t *)file_priv;

    struct spi_buf rx_buf = { .buf = buf, .len = len };
    struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_transceive(f->drv->dev, &f->zephyr_cfg, NULL, &rx_bufs);
    if (ret < 0) { return POSIX_ERR_IO; }
    return (posix_ssize_t)len;
}

/* ---------- write：发送 TX 字节，丢弃 MISO ---------- */
static posix_ssize_t spi_write_fn(void *drv_data, void *file_priv,
                                  const void *buf, size_t len)
{
    (void)drv_data;
    spi_file_t *f = (spi_file_t *)file_priv;

    struct spi_buf tx_buf = { .buf = (void *)buf, .len = len };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

    int ret = spi_transceive(f->drv->dev, &f->zephyr_cfg, &tx_bufs, NULL);
    if (ret < 0) { return POSIX_ERR_IO; }
    return (posix_ssize_t)len;
}

/* ---------- ioctl ---------- */
static int spi_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    (void)drv_data;
    spi_file_t *f = (spi_file_t *)file_priv;

    switch (cmd)
    {
    case POSIX_SPI_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            f->cfg = *(posix_spi_config_t *)arg;
            spi_build_zephyr_cfg(&f->cfg, &f->zephyr_cfg);
            return POSIX_OK;
        }

    case POSIX_SPI_IOCTL_GET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            *(posix_spi_config_t *)arg = f->cfg;
            return POSIX_OK;
        }

    case POSIX_SPI_IOCTL_TRANSFER:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_spi_transfer_t *t = (posix_spi_transfer_t *)arg;

            struct spi_buf_set *p_tx = NULL;
            struct spi_buf_set *p_rx = NULL;
            struct spi_buf      tx_buf;
            struct spi_buf      rx_buf;
            struct spi_buf_set  tx_bufs;
            struct spi_buf_set  rx_bufs;

            if (t->tx_buf)
            {
                tx_buf  = (struct spi_buf) { .buf = (void *)t->tx_buf, .len = t->len };
                tx_bufs = (struct spi_buf_set) { .buffers = &tx_buf, .count = 1 };
                p_tx    = &tx_bufs;
            }
            if (t->rx_buf)
            {
                rx_buf  = (struct spi_buf) { .buf = t->rx_buf, .len = t->len };
                rx_bufs = (struct spi_buf_set) { .buffers = &rx_buf, .count = 1 };
                p_rx    = &rx_bufs;
            }

            int ret = spi_transceive(f->drv->dev, &f->zephyr_cfg, p_tx, p_rx);
            return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
        }

    case POSIX_SPI_IOCTL_CS_TAKE:
        /* CS 由应用层控制（cs_pin = -1），直接返回成功 */
        return POSIX_OK;

    case POSIX_SPI_IOCTL_CS_RELEASE:
        /* CS 由应用层控制（cs_pin = -1），直接返回成功 */
        return POSIX_OK;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_spi_ops =
{
    .open  = spi_open,
    .close = spi_close,
    .read  = spi_read_fn,
    .write = spi_write_fn,
    .ioctl = spi_ioctl,
};

/* ---------- 设备实例 ---------- */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi0), okay)
static spi_drv_data_t s_spi0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(spi0)) };
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi1), okay)
static spi_drv_data_t s_spi1 = { .dev = DEVICE_DT_GET(DT_NODELABEL(spi1)) };
#endif

/* ---------- 自动注册 ---------- */
static int spi_init(void)
{
    int ret = POSIX_OK;
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi0), okay)
    ret = posix_device_register("/dev/spi0", &g_spi_ops, &s_spi0);
    if (ret) { return ret; }
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi1), okay)
    ret = posix_device_register("/dev/spi1", &g_spi_ops, &s_spi1);
#endif
    return ret;
}
POSIX_INIT_DEVICE_EXPORT(spi_init);
