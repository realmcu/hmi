/* ================================================================
 * PWM 驱动 — 基于 Zephyr PWM API 的 posix-device 适配层
 *
 * 路径格式: /dev/pwm<N>/ch<Channel>
 *   /dev/pwm0/chX  → pwm4，channel X
 *
 * DTS 节点映射：
 *   /dev/pwm0  → DT_NODELABEL(pwm4)
 *
 * 依赖：DTS 中 pwm4 节点已 enabled，且 CONFIG_PWM=y。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_pwm.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 控制器私有数据（每个 posix 设备一份） ---------- */
typedef struct
{
    const struct device *dev;   /* Zephyr PWM device */
} pwm_drv_data_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct
{
    bool             in_use;
    pwm_drv_data_t  *drv;
    uint32_t         channel;
    uint32_t         period_ns;
    uint32_t         pulse_ns;
    pwm_flags_t      flags;
} pwm_file_t;

#define MAX_PWM_FILES  8
static pwm_file_t s_pwm_files[MAX_PWM_FILES];

/* ---------- open：解析路径，绑定 channel ---------- */
static void *pwm_open(void *drv_data, const char *path)
{
    pwm_drv_data_t *d = (pwm_drv_data_t *)drv_data;

    /* 解析 channel 号：找 "/ch" 后面的数字 */
    uint32_t channel = 0;
    const char *p = strstr(path, "/ch");
    if (p)
    {
        p += 3;   /* 跳过 "/ch" */
        char *end;
        long ch = strtol(p, &end, 10);
        if (end != p && ch >= 0)
        {
            channel = (uint32_t)ch;
        }
    }

    if (!device_is_ready(d->dev)) { return POSIX_OPEN_ERR; }

    pwm_file_t *f = NULL;
    for (int i = 0; i < MAX_PWM_FILES; i++)
    {
        if (!s_pwm_files[i].in_use)
        {
            f = &s_pwm_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use = true;
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv       = d;
    f->channel   = channel;
    f->period_ns = 1000000UL;   /* 默认 1 ms 周期（1 kHz） */
    f->pulse_ns  = 0;
    f->flags     = PWM_POLARITY_NORMAL;
    return f;
}

/* ---------- close ---------- */
static int pwm_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    pwm_file_t *f = (pwm_file_t *)file_priv;
    if (!f || !f->in_use) { return POSIX_OK; }

    /* 停止输出 */
    pwm_set(f->drv->dev, f->channel, f->period_ns, 0, f->flags);
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read：不支持 ---------- */
static posix_ssize_t pwm_read(void *drv_data, void *file_priv,
                              void *buf, size_t count)
{
    (void)drv_data;
    (void)file_priv;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

/* ---------- write：不支持 ---------- */
static posix_ssize_t pwm_write(void *drv_data, void *file_priv,
                               const void *buf, size_t count)
{
    (void)drv_data;
    (void)file_priv;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int pwm_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    (void)drv_data;
    pwm_file_t *f = (pwm_file_t *)file_priv;

    switch (cmd)
    {
    case POSIX_PWM_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_pwm_config_t *cfg = (posix_pwm_config_t *)arg;

            /* 计算 period_ns */
            uint32_t period_ns;
            if (cfg->period_us > 0)
            {
                period_ns = cfg->period_us * 1000U;
            }
            else if (cfg->freq_hz > 0)
            {
                period_ns = 1000000000UL / cfg->freq_hz;
            }
            else
            {
                return POSIX_ERR_INVAL;
            }

            /* 计算 pulse_ns */
            uint32_t pulse_ns;
            if (cfg->pulse_us > 0)
            {
                pulse_ns = cfg->pulse_us * 1000U;
            }
            else
            {
                float duty = cfg->duty_cycle;
                if (duty < 0.0f) { duty = 0.0f; }
                if (duty > 1.0f) { duty = 1.0f; }
                pulse_ns = (uint32_t)((float)period_ns * duty);
            }

            /* 极性 */
            pwm_flags_t flags = (cfg->polarity == POSIX_PWM_POLARITY_INVERTED)
                                ? PWM_POLARITY_INVERTED
                                : PWM_POLARITY_NORMAL;

            uint32_t channel = (cfg->channel >= 0) ? (uint32_t)cfg->channel
                               : f->channel;

            int ret = pwm_set(f->drv->dev, channel, period_ns, pulse_ns, flags);
            if (ret < 0) { return POSIX_ERR_IO; }

            f->channel   = channel;
            f->period_ns = period_ns;
            f->pulse_ns  = pulse_ns;
            f->flags     = flags;
            return POSIX_OK;
        }

    case POSIX_PWM_IOCTL_SET_DUTY:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            float duty = *(float *)arg;
            if (duty < 0.0f) { duty = 0.0f; }
            if (duty > 1.0f) { duty = 1.0f; }

            uint32_t pulse_ns = (uint32_t)((float)f->period_ns * duty);
            int ret = pwm_set(f->drv->dev, f->channel, f->period_ns,
                              pulse_ns, f->flags);
            if (ret < 0) { return POSIX_ERR_IO; }

            f->pulse_ns = pulse_ns;
            return POSIX_OK;
        }

    case POSIX_PWM_IOCTL_SET_PULSE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            uint32_t pulse_us = *(uint32_t *)arg;
            uint32_t pulse_ns = pulse_us * 1000U;

            int ret = pwm_set(f->drv->dev, f->channel, f->period_ns,
                              pulse_ns, f->flags);
            if (ret < 0) { return POSIX_ERR_IO; }

            f->pulse_ns = pulse_ns;
            return POSIX_OK;
        }

    case POSIX_PWM_IOCTL_START:
        {
            int ret = pwm_set(f->drv->dev, f->channel, f->period_ns,
                              f->pulse_ns, f->flags);
            return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
        }

    case POSIX_PWM_IOCTL_STOP:
        {
            int ret = pwm_set(f->drv->dev, f->channel, f->period_ns,
                              0, f->flags);
            return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
        }

    case POSIX_PWM_IOCTL_CAPTURE_START:
    case POSIX_PWM_IOCTL_CAPTURE_STOP:
        return POSIX_ERR_NOSUPP;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_pwm_ops =
{
    .open  = pwm_open,
    .close = pwm_close,
    .read  = pwm_read,
    .write = pwm_write,
    .ioctl = pwm_ioctl,
};

/* ---------- 设备实例 ---------- */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(pwm4), okay)
static pwm_drv_data_t s_pwm0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(pwm4)) };

/* ---------- 自动注册 ---------- */
static int pwm_init(void)
{
    return posix_device_register("/dev/pwm0", &g_pwm_ops, &s_pwm0);
}
POSIX_INIT_DEVICE_EXPORT(pwm_init);
#endif
