#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gsensor.h"

typedef struct { int unit; uint8_t i2c_addr; } gsensor_drv_t;
typedef struct { gsensor_drv_t *drv; posix_gsensor_config_t cfg; } gsensor_file_t;

#define MAX_GSENSOR_FILES  2
static gsensor_file_t s_gsensor_files[MAX_GSENSOR_FILES];
static int s_gsensor_file_used[MAX_GSENSOR_FILES];

static void *gsensor_open(void *d, const char *p)
{
    (void)p;
    gsensor_file_t *f = NULL;
    for (int i = 0; i < MAX_GSENSOR_FILES; i++)
    {
        if (!s_gsensor_file_used[i]) { s_gsensor_file_used[i] = 1; f = &s_gsensor_files[i]; break; }
    }
    if (!f) { return POSIX_OPEN_ERR; }
    f->drv = (gsensor_drv_t *)d; return f;
}
static int gsensor_close(void *d, void *fv)
{
    (void)d;
    gsensor_file_t *f = (gsensor_file_t *)fv;
    int idx = f - s_gsensor_files;
    if (idx >= 0 && idx < MAX_GSENSOR_FILES) { s_gsensor_file_used[idx] = 0; }
    return 0;
}

/* posix_read = 读三轴数据 */
static posix_ssize_t gsensor_read(void *d, void *f, void *buf, size_t count)
{
    gsensor_drv_t *drv = (gsensor_drv_t *)d; (void)f;
    if (count < sizeof(posix_gsensor_axis_t)) { return POSIX_ERR_INVAL; }
    /* hw_gsensor_read_xyz(drv->unit, (posix_gsensor_axis_t *)buf); */
    (void)drv; (void)buf; (void)count;
    return (posix_ssize_t)sizeof(posix_gsensor_axis_t);
}
static posix_ssize_t gsensor_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP;
}

static int gsensor_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    gsensor_drv_t *drv = (gsensor_drv_t *)d;
    gsensor_file_t *file = (gsensor_file_t *)f;
    (void)drv;
    switch (cmd)
    {
    case POSIX_GSENSOR_IOCTL_SET_CONFIG:
        file->cfg = *(posix_gsensor_config_t *)arg;
        /* hw_gsensor_set_range(drv->unit, file->cfg.range); */
        /* hw_gsensor_set_odr(drv->unit, file->cfg.odr_hz); */
        return 0;
    case POSIX_GSENSOR_IOCTL_READ_TEMP:
        {
            int *temp = (int *)arg;
            /* *temp = hw_gsensor_read_temp(drv->unit); */
            (void)temp; return POSIX_ERR_NOSUPP;
        }
    case POSIX_GSENSOR_IOCTL_SELF_TEST:
        /* return hw_gsensor_self_test(drv->unit) ? 0 : POSIX_ERR_IO; */
        return POSIX_ERR_NOSUPP;
    default: return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_gsensor_ops =
{
    .open = gsensor_open, .close = gsensor_close, .read = gsensor_read,
    .write = gsensor_write, .ioctl = gsensor_ioctl,
};
static gsensor_drv_t s_gsensor0 = { .unit = 0, .i2c_addr = 0x68 };

/* ---------- 自动注册 ---------- */
static int gsensor_init(void)
{
    return posix_device_register("/dev/gsensor0", &g_gsensor_ops, &s_gsensor0);
}
POSIX_INIT_DEVICE_EXPORT(gsensor_init);
