/* TODO: implement with RTK I2C API when I2C driver is available */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_i2c.h"

#define MAX_I2C_FILES 4

typedef struct
{
    int      unit;
    uint32_t default_hz;
} i2c_drv_t;

typedef struct
{
    i2c_drv_t          *drv;
    posix_i2c_config_t  cfg;
    int                 in_use;
} i2c_file_t;

static i2c_file_t s_i2c_files[MAX_I2C_FILES];

static void *i2c_open(void *d, const char *p)
{
    (void)p;
    for (int i = 0; i < MAX_I2C_FILES; i++)
    {
        if (!s_i2c_files[i].in_use)
        {
            s_i2c_files[i].in_use = 1;
            s_i2c_files[i].drv    = (i2c_drv_t *)d;
            return &s_i2c_files[i];
        }
    }
    return POSIX_OPEN_ERR;
}

static int i2c_close(void *d, void *f)
{
    (void)d;
    i2c_file_t *file = (i2c_file_t *)f;
    if (file)
    {
        file->in_use = 0;
        file->drv    = NULL;
    }
    return 0;
}

static posix_ssize_t i2c_read(void *d, void *f, void *buf, size_t count)
{
    (void)d;
    (void)f;
    (void)buf;
    (void)count;
    /* I2C 没有数据流语义，read/write 恒不支持，事务走 ioctl */
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t i2c_write(void *d, void *f, const void *buf, size_t count)
{
    (void)d;
    (void)f;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

static int i2c_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    (void)d;
    (void)f;
    (void)arg;
    /* TODO: implement with RTK I2C API when I2C driver is available */
    switch (cmd)
    {
    case POSIX_I2C_IOCTL_SET_CONFIG:
    case POSIX_I2C_IOCTL_GET_CONFIG:
    case POSIX_I2C_IOCTL_WRITE_REG:
    case POSIX_I2C_IOCTL_READ_REG:
    case POSIX_I2C_IOCTL_RAW_WRITE:
    case POSIX_I2C_IOCTL_RAW_READ:
    default:
        return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_i2c_ops =
{
    .open  = i2c_open,
    .close = i2c_close,
    .read  = i2c_read,
    .write = i2c_write,
    .ioctl = i2c_ioctl,
};

static i2c_drv_t s_i2c0 =
{
    .unit       = 0,
    .default_hz = POSIX_I2C_SPEED_FAST,
};

static int i2c_init(void)
{
    return posix_device_register("/dev/i2c0", &g_i2c_ops, &s_i2c0);
}
POSIX_INIT_DEVICE_EXPORT(i2c_init);
