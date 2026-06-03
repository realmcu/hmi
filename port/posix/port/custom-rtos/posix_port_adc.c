#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_adc.h"

typedef struct { int unit; uintptr_t reg_base; } adc_drv_t;
typedef struct { adc_drv_t *drv; posix_adc_config_t cfg; } adc_file_t;

static void *adc_open(void *d, const char *p)
{
    (void)p; adc_file_t *f = (adc_file_t *)/*alloc*/;
    f->drv = (adc_drv_t *)d; return f;
}
static int adc_close(void *d, void *f) { (void)d; /*free f*/; return 0; }

/* posix_read = 单次采样（默认 channel 0） */
static int adc_read(void *d, void *f, void *buf, size_t count)
{
    (void)d; adc_file_t *file = (adc_file_t *)f;
    if (count < sizeof(uint32_t)) { return POSIX_ERR_INVAL; }
    /* *(uint32_t*)buf = hw_adc_read(file->drv->reg_base, 0); */
    (void)file; return sizeof(uint32_t);
}
static int adc_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP;
}

static int adc_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    adc_drv_t *drv = (adc_drv_t *)d;
    adc_file_t *file = (adc_file_t *)f;
    (void)drv;

    switch (cmd)
    {
    case POSIX_ADC_IOCTL_SET_CONFIG:
        file->cfg = *(posix_adc_config_t *)arg;
        /* hw_adc_config(drv->reg_base, file->cfg.resolution, file->cfg.sample_rate_hz); */
        return 0;
    case POSIX_ADC_IOCTL_READ_CHANNEL:
        {
            int ch = arg ? *(int *)arg : 0;
            /* *(uint32_t*)arg = hw_adc_read(drv->reg_base, ch); */
            (void)ch; return POSIX_ERR_NOSUPP;
        }
    default: return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_adc_ops =
{
    .open = adc_open, .close = adc_close, .read = adc_read,
    .write = adc_write, .ioctl = adc_ioctl,
};
static adc_drv_t s_adc0 = { .unit = 0, .reg_base = 0x40040000 };

/* ---------- 自动注册 ---------- */
static int adc_init(void)
{
    return posix_device_register("/dev/adc0", &g_adc_ops, &s_adc0);
}
POSIX_INIT_DEVICE_EXPORT(adc_init);
