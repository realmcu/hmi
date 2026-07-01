/* ================================================================
 * ADC 驱动 — 基于 Zephyr ADC API 的 posix-device 适配层
 *
 * 路径格式: /dev/adc0
 *
 * 依赖：DTS 中 adc 节点已 enabled，且 CONFIG_ADC=y。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_adc.h"

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <string.h>

/* 内部参考电压（mV） */
#define ADC_REF_INTERNAL_MV  3300

/* ---------- 控制器私有数据（每个 posix 设备一份） ---------- */
typedef struct
{
    const struct device *dev;   /* Zephyr ADC device */
} adc_drv_data_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct
{
    bool              in_use;
    adc_drv_data_t   *drv;
    posix_adc_config_t cfg;
    uint32_t          ch_setup_mask;   /* 已 setup 的 channel 掩码 */
} adc_file_t;

#define MAX_ADC_FILES  4
static adc_file_t s_adc_files[MAX_ADC_FILES];

/* ---------- 内部：按需 setup 一个 channel ---------- */
static int adc_ensure_channel(adc_file_t *f, uint8_t ch_id)
{
    if (f->ch_setup_mask & BIT(ch_id))
    {
        return 0;   /* 已 setup，跳过 */
    }

    /* 映射参考电压 */
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

/* ---------- 内部：采样一个 channel，返回 raw 值 ---------- */
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

    /* 默认配置 */
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

/* ---------- read：读 channel 0 的 raw 值 ---------- */
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

/* ---------- write：不支持 ---------- */
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

            /* 验证分辨率 */
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

            /* 配置变更时重置 channel setup 状态 */
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
            /* *(int*)arg 作为入参：channel 号 */
            uint8_t ch_id = (uint8_t)(*(int *)arg);
            int16_t raw   = 0;

            int ret = adc_sample_raw(f, ch_id, &raw);
            if (ret < 0) { return POSIX_ERR_IO; }

            /* raw 采样值写回同一个 arg */
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

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_adc_ops =
{
    .open  = adc_open,
    .close = adc_close,
    .read  = adc_read_fn,
    .write = adc_write,
    .ioctl = adc_ioctl,
};

/* ---------- 设备实例 ---------- */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(adc), okay)
static adc_drv_data_t s_adc0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(adc)) };

/* ---------- 自动注册 ---------- */
static int adc_init(void)
{
    return posix_device_register("/dev/adc0", &g_adc_ops, &s_adc0);
}
POSIX_INIT_DEVICE_EXPORT(adc_init);
#endif
