#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_spi.h"
#include <stdbool.h>

typedef struct { int unit; uintptr_t reg_base; } spi_drv_t;
typedef struct { bool in_use; spi_drv_t *drv; posix_spi_config_t cfg; } spi_file_t;

#define MAX_SPI_FILES  4
static spi_file_t s_spi_files[MAX_SPI_FILES];

static void *spi_open(void *d, const char *path)
{
    (void)path;
    spi_file_t *f = NULL;
    for (int i = 0; i < MAX_SPI_FILES; i++)
    {
        if (!s_spi_files[i].in_use) { s_spi_files[i].in_use = true; f = &s_spi_files[i]; break; }
    }
    if (!f) { return POSIX_OPEN_ERR; }
    f->drv = (spi_drv_t *)d;
    return f;
}

static int spi_close(void *d, void *fv)
{
    (void)d;
    ((spi_file_t *)fv)->in_use = false;
    return 0;
}

static posix_ssize_t spi_read(void *d, void *f, void *buf, size_t len)
{
    (void)f;
    /* 发 0xFF 收数据 */
    /* hw_spi_transfer(((spi_drv_t*)d)->reg_base, NULL, buf, len); */
    (void)d; (void)buf; (void)len;
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t spi_write(void *d, void *f, const void *buf, size_t len)
{
    (void)f;
    /* hw_spi_transfer(((spi_drv_t*)d)->reg_base, buf, NULL, len); */
    (void)d; (void)buf; (void)len;
    return POSIX_ERR_NOSUPP;
}

static int spi_ioctl(void *d, void *fv, unsigned long cmd, void *arg)
{
    spi_drv_t  *drv  = (spi_drv_t *)d;
    spi_file_t *file = (spi_file_t *)fv;
    switch (cmd)
    {
    case POSIX_SPI_IOCTL_SET_CONFIG:
        {
            posix_spi_config_t *c = (posix_spi_config_t *)arg;
            /* hw_spi_config(drv->reg_base, c->mode, c->freq_hz, c->bits_per_word); */
            file->cfg = *c;
            (void)drv;
            return 0;
        }
    case POSIX_SPI_IOCTL_TRANSFER:
        {
            posix_spi_transfer_t *t = (posix_spi_transfer_t *)arg;
            /* hw_spi_transfer(drv->reg_base, t->tx_buf, t->rx_buf, t->len); */
            (void)t; (void)drv; (void)file;
            return POSIX_ERR_NOSUPP;
        }
    case POSIX_SPI_IOCTL_CS_TAKE:
        /* hw_spi_cs_take(drv->reg_base); */
        (void)drv; (void)file;
        return 0;
    case POSIX_SPI_IOCTL_CS_RELEASE:
        /* hw_spi_cs_release(drv->reg_base); */
        (void)drv; (void)file;
        return 0;
    default:
        return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_spi_ops =
{
    .open = spi_open, .close = spi_close, .read = spi_read, .write = spi_write, .ioctl = spi_ioctl
};

static spi_drv_t s_spi0 = { .unit = 0, .reg_base = 0x40020000 };
static spi_drv_t s_spi1 = { .unit = 1, .reg_base = 0x40021000 };

static int spi_init(void)
{
    void *privs[] = { &s_spi0, &s_spi1 };
    return posix_device_register_group("/dev/spi%d", 2, &g_spi_ops, privs);
}
POSIX_INIT_DEVICE_EXPORT(spi_init);
