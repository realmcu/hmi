#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_pwm.h"

/* PWM 没有流式数据，read/write 返回 NOSUPP，全部通过 ioctl 控制 */

typedef struct
{
    int unit;
    uintptr_t reg_base;
} pwm_drv_t;

typedef struct
{
    pwm_drv_t *drv;
    int channel;
} pwm_file_t;

static void *pwm_open(void *d, const char *path)
{
    /* 解析 channel: /dev/pwm0 的默认 channel=0
     * 或 /dev/pwm0/ch1 指定 channel */
    pwm_file_t *f = (pwm_file_t *)/*alloc*/;
    f->drv = (pwm_drv_t *)d;
    f->channel = 0; /* 默认 channel 0 */
    /* 如果路径中有 /chN 则解析 channel 号 */
    const char *ch = strstr(path, "/ch");
    if (ch)
    {
        ch += 3;
        f->channel = atoi(ch);
    }
    return f;
}

static int pwm_close(void *d, void *f) { (void)d; /*free f*/; return 0; }
static posix_ssize_t pwm_read(void *d, void *f, void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP;
}
static posix_ssize_t pwm_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP;
}

static int pwm_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    pwm_drv_t *drv = (pwm_drv_t *)d;
    pwm_file_t *file = (pwm_file_t *)f;
    (void)drv;

    switch (cmd)
    {
    case POSIX_PWM_IOCTL_SET_CONFIG:
        {
            posix_pwm_config_t *cfg = (posix_pwm_config_t *)arg;
            int ch = cfg->channel;
            /* hw_pwm_set_freq(drv->reg_base, ch, cfg->freq_hz); */
            /* hw_pwm_set_duty(drv->reg_base, ch, cfg->duty_cycle); */
            file->channel = ch;
            (void)cfg;
            return 0;
        }
    case POSIX_PWM_IOCTL_START:
        {
            int ch = arg ? *(int *)arg : file->channel;
            /* hw_pwm_start(drv->reg_base, ch); */
            (void)ch; return 0;
        }
    case POSIX_PWM_IOCTL_STOP:
        {
            int ch = arg ? *(int *)arg : file->channel;
            /* hw_pwm_stop(drv->reg_base, ch); */
            (void)ch; return 0;
        }
    default: return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_pwm_ops =
{
    .open = pwm_open, .close = pwm_close, .read = pwm_read,
    .write = pwm_write, .ioctl = pwm_ioctl,
};

static pwm_drv_t s_pwm0 = { .unit = 0, .reg_base = 0x40030000 };

/* ---------- 自动注册 ---------- */
static int pwm_init(void)
{
    return posix_device_register("/dev/pwm0", &g_pwm_ops, &s_pwm0);
}
POSIX_INIT_DEVICE_EXPORT(pwm_init);
