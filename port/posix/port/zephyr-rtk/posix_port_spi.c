/**
 * @file posix_port_spi.c
 * @brief RTL8773G SPI driver stub for POSIX abstraction layer.
 *
 * Registers /dev/spi0 (and /dev/spi1) via POSIX_INIT_DEVICE_EXPORT.
 * Actual RTK SDK hardware calls are stubbed out with comments.
 */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_spi.h"

/* RTK SPI SDK header (stubbed - actual HW calls commented out) */
/* #include "rtl876x_spi.h" */
#include <stdbool.h>

/* ---------- tunables ---------------------------------------------------- */

#define MAX_SPI_FILES  4   /* static file-handle pool size                   */
#define SPI_NUM_UNITS  2   /* number of SPI controllers on RTL8773G          */

/* ---------- private types ------------------------------------------------ */

typedef struct
{
    int      unit;      /* controller index (0 = SPI0, 1 = SPI1)            */
    uint32_t reg_base;  /* peripheral base address                           */
} spi_drv_t;

typedef struct
{
    spi_drv_t        *drv;  /* back-pointer to the driver/controller context */
    posix_spi_config_t cfg; /* current configuration                         */
    bool               used;
} spi_file_t;

/* ---------- static pools ------------------------------------------------- */

static spi_drv_t s_spi0 = { .unit = 0, .reg_base = 0x40020000UL };
static spi_drv_t s_spi1 = { .unit = 1, .reg_base = 0x40021000UL };

static spi_file_t s_file_pool[MAX_SPI_FILES];

/* ---------- pool helpers ------------------------------------------------- */

static spi_file_t *spi_alloc_file(void)
{
    for (int i = 0; i < MAX_SPI_FILES; i++)
    {
        if (!s_file_pool[i].used)
        {
            s_file_pool[i].used = true;
            return &s_file_pool[i];
        }
    }
    return NULL;
}

static void spi_free_file(spi_file_t *f)
{
    if (f)
    {
        f->used = false;
        f->drv  = NULL;
    }
}

/* ---------- driver ops --------------------------------------------------- */

static void *spi_open(void *d, const char *path)
{
    (void)path;

    spi_file_t *f = spi_alloc_file();
    if (!f)
    {
        return NULL;
    }

    f->drv = (spi_drv_t *)d;

    /* default configuration */
    f->cfg.freq_hz      = 1000000U;
    f->cfg.mode         = POSIX_SPI_MODE_0;
    f->cfg.bits_per_word = 8U;
    f->cfg.cs_pin       = -1;

    /* RTK SDK: SPI_DeInit / SPI_Init would go here, e.g.:
     *   SPI_DeInit(SPI0);
     *   SPI_InitTypeDef spi_cfg = {0};
     *   SPI_StructInit(&spi_cfg);
     *   spi_cfg.SPI_Direction   = SPI_Direction_FullDuplex;
     *   spi_cfg.SPI_Mode        = SPI_Mode_Master;
     *   spi_cfg.SPI_DataSize    = SPI_DataSize_8b;
     *   spi_cfg.SPI_CPOL        = SPI_CPOL_Low;
     *   spi_cfg.SPI_CPHA        = SPI_CPHA_1Edge;
     *   spi_cfg.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
     *   SPI_Init(SPI0, &spi_cfg);
     *   SPI_Cmd(SPI0, ENABLE);
     */

    return (void *)f;
}

static int spi_close(void *d, void *fh)
{
    (void)d;
    spi_file_t *f = (spi_file_t *)fh;

    /* RTK SDK: SPI_Cmd(SPI0, DISABLE); */

    spi_free_file(f);
    return 0;
}

static int spi_read(void *d, void *fh, void *buf, size_t len)
{
    (void)fh;
    (void)d;
    (void)buf;
    (void)len;

    /* Read-only transfer: send 0xFF dummy bytes while capturing MISO.
     * RTK SDK stub:
     *   spi_drv_t *drv = (spi_drv_t *)d;
     *   (void)drv->reg_base;
     *   uint8_t dummy[len];
     *   memset(dummy, 0xFF, len);
     *   SPI_SendBuf(SPI0, dummy, (uint8_t *)buf, len);
     */

    return POSIX_ERR_NOSUPP;
}

static int spi_write(void *d, void *fh, const void *buf, size_t len)
{
    (void)fh;
    (void)d;
    (void)buf;
    (void)len;

    /* Write-only transfer: discard MISO.
     * RTK SDK stub:
     *   spi_drv_t *drv = (spi_drv_t *)d;
     *   (void)drv->reg_base;
     *   SPI_SendBuf(SPI0, (uint8_t *)buf, NULL, len);
     */

    return POSIX_ERR_NOSUPP;
}

static int spi_ioctl(void *d, void *fh, unsigned long cmd, void *arg)
{
    spi_drv_t  *drv = (spi_drv_t *)d;
    spi_file_t *f   = (spi_file_t *)fh;

    (void)drv; /* suppress unused-variable warning when stubs are active */

    switch (cmd)
    {

    case POSIX_SPI_IOCTL_SET_CONFIG:
        {
            if (!arg)
            {
                return POSIX_ERR_INVAL;
            }
            posix_spi_config_t *c = (posix_spi_config_t *)arg;

            /* RTK SDK stub:
             *   SPI_InitTypeDef spi_cfg = {0};
             *   SPI_StructInit(&spi_cfg);
             *   spi_cfg.SPI_BaudRatePrescaler = freq_to_prescaler(c->freq_hz);
             *   spi_cfg.SPI_CPOL = (c->mode & 0x02) ? SPI_CPOL_High : SPI_CPOL_Low;
             *   spi_cfg.SPI_CPHA = (c->mode & 0x01) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
             *   spi_cfg.SPI_DataSize = (c->bits_per_word == 16)
             *                          ? SPI_DataSize_16b : SPI_DataSize_8b;
             *   SPI_Init(SPI0, &spi_cfg);
             */

            f->cfg = *c;
            return 0;
        }

    case POSIX_SPI_IOCTL_GET_CONFIG:
        {
            if (!arg)
            {
                return POSIX_ERR_INVAL;
            }
            *(posix_spi_config_t *)arg = f->cfg;
            return 0;
        }

    case POSIX_SPI_IOCTL_TRANSFER:
        {
            if (!arg)
            {
                return POSIX_ERR_INVAL;
            }
            posix_spi_transfer_t *t = (posix_spi_transfer_t *)arg;
            (void)t;

            /* Full-duplex transfer.
             * RTK SDK stub:
             *   SPI_SendBuf(SPI0,
             *               (uint8_t *)t->tx_buf,
             *               (uint8_t *)t->rx_buf,
             *               (uint16_t)t->len);
             */

            return POSIX_ERR_NOSUPP;
        }

    case POSIX_SPI_IOCTL_CS_TAKE:
        /* Assert chip-select.
         * RTK SDK / GPIO stub:
         *   if (f->cfg.cs_pin >= 0) {
         *       uint8_t active = (f->cfg.cs_pin & 0x100)
         *                        ? POSIX_SPI_CS_ACTIVE_HIGH
         *                        : POSIX_SPI_CS_ACTIVE_LOW;
         *       GPIO_WriteBit(GPIO_GetPort(f->cfg.cs_pin),
         *                    GPIO_GetPin(f->cfg.cs_pin),
         *                    (active == POSIX_SPI_CS_ACTIVE_LOW) ? Bit_RESET : Bit_SET);
         *   }
         */
        return 0;

    case POSIX_SPI_IOCTL_CS_RELEASE:
        /* De-assert chip-select.
         * RTK SDK / GPIO stub:
         *   if (f->cfg.cs_pin >= 0) {
         *       uint8_t active = (f->cfg.cs_pin & 0x100)
         *                        ? POSIX_SPI_CS_ACTIVE_HIGH
         *                        : POSIX_SPI_CS_ACTIVE_LOW;
         *       GPIO_WriteBit(GPIO_GetPort(f->cfg.cs_pin),
         *                    GPIO_GetPin(f->cfg.cs_pin),
         *                    (active == POSIX_SPI_CS_ACTIVE_LOW) ? Bit_SET : Bit_RESET);
         *   }
         */
        return 0;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- driver ops table --------------------------------------------- */

const posix_driver_ops_t g_spi_ops =
{
    .open  = spi_open,
    .close = spi_close,
    .read  = spi_read,
    .write = spi_write,
    .ioctl = spi_ioctl,
};

/* ---------- auto-registration -------------------------------------------- */

static int spi_init(void)
{
    void *privs[SPI_NUM_UNITS] = { &s_spi0, &s_spi1 };
    return posix_device_register_group("/dev/spi%d", SPI_NUM_UNITS,
                                       &g_spi_ops, privs);
}

POSIX_INIT_DEVICE_EXPORT(spi_init);
