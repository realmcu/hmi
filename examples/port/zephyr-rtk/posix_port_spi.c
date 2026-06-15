/**
 * @file posix_port_spi.c
 * @brief RTL8773G SPI driver for POSIX abstraction layer.
 *
 * Uses RTK HAL directly (same pattern as spi_rtl87x3g.c).
 * Polling mode — no interrupt, no DMA.
 */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_spi.h"
#include "rtl876x_spi.h"

#include <stdbool.h>

/* ---------- tunables ---------------------------------------------------- */

#define SPI_BUS_FREQ_HZ   40000000U  /* RTL87x3G SPI source clock: 40 MHz   */
#define MAX_SPI_FILES     4
#define SPI_NUM_UNITS     2

/* ---------- private types ------------------------------------------------ */

typedef struct
{
    int          unit;
    SPI_TypeDef *spi;   /* RTK peripheral pointer (SPI0 / SPI1)              */
} spi_drv_t;

typedef struct
{
    spi_drv_t         *drv;
    posix_spi_config_t cfg;
    bool               used;
} spi_file_t;

/* ---------- static instances & pool ------------------------------------- */

static spi_drv_t s_spi0 = { .unit = 0, .spi = (SPI_TypeDef *)0x40020000UL };
static spi_drv_t s_spi1 = { .unit = 1, .spi = (SPI_TypeDef *)0x40021000UL };

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
    if (f) { f->used = false; f->drv = NULL; }
}

/* ---------- hardware helpers -------------------------------------------- */

/* Apply posix_spi_config_t to hardware.
 * Pattern mirrors spi_rtl87x3g_configure() in spi_rtl87x3g.c. */
static void spi_apply_config(SPI_TypeDef *spi, const posix_spi_config_t *cfg)
{
    SPI_Cmd(spi, DISABLE);

    SPI_InitTypeDef init;
    SPI_StructInit(&init);

    /* Bus clock / target frequency — same calculation as spi_rtl87x3g.c:
     *   SPI_BaudRatePrescaler = bus_freq / frequency                        */
    uint32_t prescaler = (cfg->freq_hz > 0U) ? (SPI_BUS_FREQ_HZ / cfg->freq_hz) : 40U;
    if (prescaler < 1U) { prescaler = 1U; }
    init.SPI_BaudRatePrescaler = (uint16_t)prescaler;

    /* SPI_DataSize = word_size - 1  (RTK uses N-1 encoding)
     * mirrors: spi_init_struct.SPI_DataSize = SPI_WORD_SIZE_GET(op) - 1    */
    init.SPI_DataSize = (uint16_t)(cfg->bits_per_word - 1U);

    /* CPOL / CPHA from POSIX mode (bit1=CPOL, bit0=CPHA)
     * mirrors: SPI_CPOL = op & SPI_MODE_CPOL ? High : Low                  */
    init.SPI_CPOL = (cfg->mode & 0x02U) ? SPI_CPOL_High : SPI_CPOL_Low;
    init.SPI_CPHA = (cfg->mode & 0x01U) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;

    init.SPI_TxThresholdLevel = 0;
    init.SPI_RxThresholdLevel = 0;

    SPI_Init(spi, &init);
    SPI_Cmd(spi, ENABLE);
}

/* Polling full-duplex transfer (byte-by-byte).
 * Pattern from spi_rtl87x3g_frame_exchange():
 *   - SPI_SendData()    to push TX frame into FIFO
 *   - SPI_ReceiveData() to drain RX frame from FIFO
 *   - Wait SPI_FLAG_TFE + !SPI_FLAG_BUSY for completion               */
static int spi_do_transfer(SPI_TypeDef *spi,
                           const uint8_t *tx, uint8_t *rx, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        /* Wait for TX FIFO to have room (TxFIFOLen > 0 means occupied) */
        while (SPI_GetTxFIFOLen(spi) != 0 ||
               SPI_GetFlagState(spi, SPI_FLAG_BUSY)) {}

        SPI_SendData(spi, tx ? (uint16_t)tx[i] : 0x00U);

        /* Wait for RX FIFO to have the echoed frame */
        while (SPI_GetRxFIFOLen(spi) == 0) {}

        uint16_t frame = SPI_ReceiveData(spi);
        if (rx) { rx[i] = (uint8_t)frame; }
    }

    /* Wait until TX FIFO empty and hardware not busy */
    while (!SPI_GetFlagState(spi, SPI_FLAG_TFE) ||
           SPI_GetFlagState(spi, SPI_FLAG_BUSY)) {}

    return (int)len;
}

/* ---------- driver ops --------------------------------------------------- */

static void *spi_open(void *d, const char *path)
{
    (void)path;

    spi_drv_t  *drv = (spi_drv_t *)d;
    spi_file_t *f   = spi_alloc_file();
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv               = drv;
    f->cfg.freq_hz       = 1000000U;
    f->cfg.mode          = POSIX_SPI_MODE_0;
    f->cfg.bits_per_word = 8U;
    f->cfg.cs_pin        = -1;

    spi_apply_config(drv->spi, &f->cfg);
    return (void *)f;
}

static int spi_close(void *d, void *fh)
{
    (void)d;
    spi_file_t *f = (spi_file_t *)fh;

    SPI_Cmd(f->drv->spi, DISABLE);
    spi_free_file(f);
    return 0;
}

/* Read-only: send 0x00 dummy bytes, capture MISO */
static posix_ssize_t spi_read_fn(void *d, void *fh, void *buf, size_t len)
{
    (void)d;
    spi_file_t *f = (spi_file_t *)fh;
    return (posix_ssize_t)spi_do_transfer(f->drv->spi, NULL, (uint8_t *)buf, len);
}

/* Write-only: send TX bytes, discard MISO */
static posix_ssize_t spi_write_fn(void *d, void *fh, const void *buf, size_t len)
{
    (void)d;
    spi_file_t *f = (spi_file_t *)fh;
    return (posix_ssize_t)spi_do_transfer(f->drv->spi, (const uint8_t *)buf, NULL, len);
}

static int spi_ioctl(void *d, void *fh, unsigned long cmd, void *arg)
{
    (void)d;
    spi_file_t *f = (spi_file_t *)fh;

    switch (cmd)
    {
    case POSIX_SPI_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            f->cfg = *(posix_spi_config_t *)arg;
            spi_apply_config(f->drv->spi, &f->cfg);
            return 0;
        }

    case POSIX_SPI_IOCTL_GET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            *(posix_spi_config_t *)arg = f->cfg;
            return 0;
        }

    case POSIX_SPI_IOCTL_TRANSFER:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_spi_transfer_t *t = (posix_spi_transfer_t *)arg;
            return spi_do_transfer(f->drv->spi,
                                   (const uint8_t *)t->tx_buf,
                                   (uint8_t *)t->rx_buf,
                                   t->len);
        }

    case POSIX_SPI_IOCTL_CS_TAKE:
        /* CS managed externally by application (cs_pin = -1 convention) */
        return 0;

    case POSIX_SPI_IOCTL_CS_RELEASE:
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
    .read  = spi_read_fn,
    .write = spi_write_fn,
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
