/* ================================================================
 * ADC driver — a posix-device adaptation layer on top of the Zephyr ADC API
 *
 * Path format: /dev/adc0
 *
 * Dependency: the adc node is enabled in the DTS, and CONFIG_ADC=y.
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_adc.h"

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <string.h>

/* Internal reference voltage (mV) */
#define ADC_REF_INTERNAL_MV  3300

/* ---------- Controller private data (one per posix device) ---------- */
typedef struct
{
    const struct device *dev;   /* Zephyr ADC device */
} adc_drv_data_t;

/* ---------- per-open file private data ---------- */
typedef struct
{
    bool              in_use;
    adc_drv_data_t   *drv;
    posix_adc_config_t cfg;
    uint32_t          ch_setup_mask;   /* mask of already set-up channels */
} adc_file_t;

#define MAX_ADC_FILES  4
static adc_file_t s_adc_files[MAX_ADC_FILES];

/* ---------- Internal: set up a channel on demand ---------- */
static int adc_ensure_channel(adc_file_t *f, uint8_t ch_id)
{
    if (f->ch_setup_mask & BIT(ch_id))
    {
        return 0;   /* already set up, skip */
    }

    /* Map the reference voltage */
    uint8_t zephyr_ref;
    switch (f->cfg.reference)
    {
    case POSIX_ADC_REF_EXTERNAL:
        zephyr_ref = ADC_REF_EXTERNAL0;
        break;
    case POSIX_ADC_REF_VDDA:
        zephyr_ref = ADC_REF_VDD_1;
        break;
    case POSIX_ADC_REF_INTERNAL:
    default:
        zephyr_ref = ADC_REF_INTERNAL;
        break;
    }

    struct adc_channel_cfg ch_cfg =
    {
        .gain             = ADC_GAIN_1,
        .reference        = zephyr_ref,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id       = ch_id,
        .differential     = 0,
    };

    int ret = adc_channel_setup(f->drv->dev, &ch_cfg);
    if (ret < 0) { return ret; }

    f->ch_setup_mask |= BIT(ch_id);
    return 0;
}

/* ---------- Internal: sample a channel, returning the raw value ---------- */
static int adc_sample_raw(adc_file_t *f, uint8_t ch_id, int16_t *out_raw)
{
    int ret = adc_ensure_channel(f, ch_id);
    if (ret < 0) { return ret; }

    static int16_t s_buf[1];

    struct adc_sequence seq =
    {
        .options     = NULL,
        .channels    = BIT(ch_id),
        .buffer      = s_buf,
        .buffer_size = sizeof(s_buf),
        .resolution  = (uint8_t)f->cfg.resolution,
        .oversampling = 0,
        .calibrate   = false,
    };

    ret = adc_read(f->drv->dev, &seq);
    if (ret < 0) { return ret; }

    *out_raw = s_buf[0];
    return 0;
}

/* ---------- open ---------- */
static void *adc_open(void *drv_data, const char *path)
{
    (void)path;
    adc_drv_data_t *d = (adc_drv_data_t *)drv_data;

    if (!device_is_ready(d->dev)) { return POSIX_OPEN_ERR; }

    adc_file_t *f = NULL;
    for (int i = 0; i < MAX_ADC_FILES; i++)
    {
        if (!s_adc_files[i].in_use)
        {
            f = &s_adc_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use = true;
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv = d;

    /* Default config */
    f->cfg.resolution    = POSIX_ADC_RES_12BIT;
    f->cfg.reference     = POSIX_ADC_REF_INTERNAL;
    f->cfg.sample_rate_hz = 0;

    return f;
}

/* ---------- close ---------- */
static int adc_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    adc_file_t *f = (adc_file_t *)file_priv;
    if (!f || !f->in_use) { return POSIX_OK; }
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read: read the raw value of channel 0 ---------- */
static posix_ssize_t adc_read_fn(void *drv_data, void *file_priv,
                                 void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(uint32_t)) { return POSIX_ERR_INVAL; }

    adc_file_t *f = (adc_file_t *)file_priv;
    int16_t raw   = 0;

    int ret = adc_sample_raw(f, 0, &raw);
    if (ret < 0) { return POSIX_ERR_IO; }

    *(uint32_t *)buf = (uint32_t)(uint16_t)raw;
    return (posix_ssize_t)sizeof(uint32_t);
}

/* ---------- write: not supported ---------- */
static posix_ssize_t adc_write(void *drv_data, void *file_priv,
                               const void *buf, size_t count)
{
    (void)drv_data;
    (void)file_priv;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int adc_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    (void)drv_data;
    adc_file_t *f = (adc_file_t *)file_priv;

    switch (cmd)
    {
    case POSIX_ADC_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_adc_config_t *cfg = (posix_adc_config_t *)arg;

            /* Validate the resolution */
            switch (cfg->resolution)
            {
            case POSIX_ADC_RES_8BIT:
            case POSIX_ADC_RES_10BIT:
            case POSIX_ADC_RES_12BIT:
            case POSIX_ADC_RES_14BIT:
            case POSIX_ADC_RES_16BIT:
                break;
            default:
                return POSIX_ERR_INVAL;
            }

            /* Reset channel setup state when the config changes */
            f->ch_setup_mask = 0;
            f->cfg = *cfg;
            return POSIX_OK;
        }

    case POSIX_ADC_IOCTL_GET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            *(posix_adc_config_t *)arg = f->cfg;
            return POSIX_OK;
        }

    case POSIX_ADC_IOCTL_READ_CHANNEL:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            /* *(int*)arg is the input: channel number */
            uint8_t ch_id = (uint8_t)(*(int *)arg);
            int16_t raw   = 0;

            int ret = adc_sample_raw(f, ch_id, &raw);
            if (ret < 0) { return POSIX_ERR_IO; }

            /* write the raw sample value back to the same arg */
            *(uint32_t *)arg = (uint32_t)(uint16_t)raw;
            return POSIX_OK;
        }

    case POSIX_ADC_IOCTL_READ_CHANNEL_MV:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            uint8_t ch_id = (uint8_t)(*(int *)arg);
            int16_t raw   = 0;

            int ret = adc_sample_raw(f, ch_id, &raw);
            if (ret < 0) { return POSIX_ERR_IO; }

            int32_t mv = (int32_t)raw;
            ret = adc_raw_to_millivolts(ADC_REF_INTERNAL_MV,
                                        ADC_GAIN_1,
                                        (uint8_t)f->cfg.resolution,
                                        &mv);
            if (ret < 0) { return POSIX_ERR_IO; }

            *(uint32_t *)arg = (uint32_t)mv;
            return POSIX_OK;
        }

    case POSIX_ADC_IOCTL_READ_MULTI:
    case POSIX_ADC_IOCTL_START_CONTINUOUS:
    case POSIX_ADC_IOCTL_STOP_CONTINUOUS:
        return POSIX_ERR_NOSUPP;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- Driver ops table ---------- */
static const posix_driver_ops_t g_adc_ops =
{
    .open  = adc_open,
    .close = adc_close,
    .read  = adc_read_fn,
    .write = adc_write,
    .ioctl = adc_ioctl,
};

/* ---------- Device instance ---------- */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(adc), okay)
static adc_drv_data_t s_adc0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(adc)) };

/* ---------- Auto-registration ---------- */
static int adc_init(void)
{
    return posix_device_register("/dev/adc0", &g_adc_ops, &s_adc0);
}
POSIX_INIT_DEVICE_EXPORT(adc_init);
#endif
