/* TODO: implement with RTK I2C API when gsensor driver is available */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gsensor.h"

#define MAX_GSENSOR_FILES 2

typedef struct
{
    int     unit;
    uint8_t i2c_addr;
} gsensor_drv_t;

typedef struct
{
    gsensor_drv_t        *drv;
    posix_gsensor_config_t cfg;
    int                   in_use;
} gsensor_file_t;

static gsensor_file_t s_gsensor_files[MAX_GSENSOR_FILES];

static void *gsensor_open(void *d, const char *p)
{
    (void)p;
    for (int i = 0; i < MAX_GSENSOR_FILES; i++)
    {
        if (!s_gsensor_files[i].in_use)
        {
            s_gsensor_files[i].in_use = 1;
            s_gsensor_files[i].drv    = (gsensor_drv_t *)d;
            return &s_gsensor_files[i];
        }
    }
    return POSIX_OPEN_ERR;
}

static int gsensor_close(void *d, void *f)
{
    (void)d;
    gsensor_file_t *file = (gsensor_file_t *)f;
    if (file)
    {
        file->in_use = 0;
        file->drv    = NULL;
    }
    return 0;
}

static posix_ssize_t gsensor_read(void *d, void *f, void *buf, size_t count)
{
    (void)d;
    (void)f;
    (void)buf;
    (void)count;
    /* TODO: implement with RTK I2C API when gsensor driver is available */
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t gsensor_write(void *d, void *f, const void *buf, size_t count)
{
    (void)d;
    (void)f;
    (void)buf;
    (void)count;
    /* TODO: implement with RTK I2C API when gsensor driver is available */
    return POSIX_ERR_NOSUPP;
}

static int gsensor_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    (void)d;
    (void)f;
    (void)arg;
    /* TODO: implement with RTK I2C API when gsensor driver is available */
    switch (cmd)
    {
    case POSIX_GSENSOR_IOCTL_SET_CONFIG:
    case POSIX_GSENSOR_IOCTL_GET_CONFIG:
    case POSIX_GSENSOR_IOCTL_SET_POWER:
    case POSIX_GSENSOR_IOCTL_SELF_TEST:
    case POSIX_GSENSOR_IOCTL_READ_TEMP:
    default:
        return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_gsensor_ops =
{
    .open  = gsensor_open,
    .close = gsensor_close,
    .read  = gsensor_read,
    .write = gsensor_write,
    .ioctl = gsensor_ioctl,
};

static gsensor_drv_t s_gsensor0 =
{
    .unit     = 0,
    .i2c_addr = 0x68,
};

static int gsensor_init(void)
{
    return posix_device_register("/dev/gsensor0", &g_gsensor_ops, &s_gsensor0);
}
POSIX_INIT_DEVICE_EXPORT(gsensor_init);
