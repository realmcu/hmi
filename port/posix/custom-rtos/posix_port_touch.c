#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_touch.h"
#include <string.h>

typedef struct { int unit; uint8_t i2c_addr; } touch_drv_t;
typedef struct { touch_drv_t *drv; posix_touch_config_t cfg; } touch_file_t;

static void *touch_open(void *d, const char *p)
{
    (void)p; touch_file_t *f = (touch_file_t *)/*alloc*/;
    f->drv = (touch_drv_t *)d; f->cfg.i2c_addr = f->drv->i2c_addr; return f;
}
static int touch_close(void *d, void *f) { (void)d; /*free f*/; return 0; }

/* posix_read = 读取触摸数据 */
static posix_ssize_t touch_read(void *d, void *f, void *buf, size_t count)
{
    touch_drv_t *drv = (touch_drv_t *)d; (void)f;
    if (count < sizeof(posix_touch_data_t)) { return POSIX_ERR_INVAL; }
    /* posix_touch_data_t *t = (posix_touch_data_t *)buf; */
    /* hw_touch_read(drv->unit, t); */
    /* return sizeof(posix_touch_data_t); */
    (void)drv; (void)buf; (void)count; memset(buf, 0, sizeof(posix_touch_data_t));
    return (posix_ssize_t)sizeof(posix_touch_data_t);
}
static posix_ssize_t touch_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP;
}

static int touch_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    touch_drv_t *drv = (touch_drv_t *)d; touch_file_t *file = (touch_file_t *)f;
    (void)drv;
    switch (cmd)
    {
    case POSIX_TOUCH_IOCTL_SET_CONFIG:
        file->cfg = *(posix_touch_config_t *)arg;
        /* hw_touch_init(drv->unit, file->cfg.i2c_addr); */
        return 0;
    case POSIX_TOUCH_IOCTL_CALIBRATE:
        /* hw_touch_calibrate(drv->unit); */
        return 0;
    case POSIX_TOUCH_IOCTL_SET_POWER:
        {
            int on = *(int *)arg;
            /* hw_touch_set_power(drv->unit, on); */
            (void)on; return 0;
        }
    default: return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_touch_ops =
{
    .open = touch_open, .close = touch_close, .read = touch_read,
    .write = touch_write, .ioctl = touch_ioctl,
};
static touch_drv_t s_touch0 = { .unit = 0, .i2c_addr = 0x38 };

/* ---------- 自动注册 ---------- */
static int touch_init(void)
{
    return posix_device_register("/dev/touch0", &g_touch_ops, &s_touch0);
}
POSIX_INIT_DEVICE_EXPORT(touch_init);
