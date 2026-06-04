/**
 * @file posix_port_touch.c
 * @brief POSIX touch port for RTK8773G + Zephyr (CHSC6417)
 */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_touch.h"
#include "touch_CHSC6417_zephyr.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <string.h>

/* ---------- driver-level data (one per chip) ---------- */

typedef struct
{
    const struct device *zephyr_dev;
} touch_drv_data_t;

/* ---------- file-level data (one per open fd) ---------- */

#define MAX_TOUCH_FILES 2

typedef struct
{
    touch_drv_data_t   *drv;
    posix_touch_config_t cfg;
    int                  in_use;
} touch_file_t;

static touch_file_t      s_touch_files[MAX_TOUCH_FILES];
static touch_drv_data_t  s_touch_drv0;

/* ---------- ops ---------- */

static void *touch_open(void *d, const char *p)
{
    (void)p;
    for (int i = 0; i < MAX_TOUCH_FILES; i++)
    {
        if (!s_touch_files[i].in_use)
        {
            s_touch_files[i].in_use = 1;
            s_touch_files[i].drv    = (touch_drv_data_t *)d;
            memset(&s_touch_files[i].cfg, 0, sizeof(posix_touch_config_t));
            return &s_touch_files[i];
        }
    }
    return POSIX_OPEN_ERR;
}

static int touch_close(void *d, void *f)
{
    (void)d;
    touch_file_t *file = (touch_file_t *)f;
    if (file)
    {
        file->in_use = 0;
        file->drv    = NULL;
    }
    return 0;
}

/*
 * touch_read - read one touch snapshot.
 *
 * Calls the Zephyr CHSC6417 driver via get_raw_touch_data(), then maps the
 * result into the POSIX touch abstraction.  The Zephyr device handle lives in
 * drv_data that was resolved at init time; the app layer never sees Zephyr APIs.
 */
static posix_ssize_t touch_read(void *d, void *f, void *buf, size_t count)
{
    (void)f;
    touch_drv_data_t *drv = (touch_drv_data_t *)d;

    if (count < sizeof(posix_touch_data_t))
    {
        return POSIX_ERR_INVAL;
    }

    posix_touch_data_t *data = (posix_touch_data_t *)buf;
    memset(data, 0, sizeof(posix_touch_data_t));

    TOUCH_DATA raw = get_raw_touch_data(drv->zephyr_dev);

    if (raw.is_press)
    {
        data->point_count         = 1;
        data->points[0].touch_id  = 0;
        data->points[0].status    = POSIX_TOUCH_PRESS;
        data->points[0].x         = raw.x;
        data->points[0].y         = raw.y;
        data->points[0].pressure  = raw.count_pressing; /* reuse pressing count as pressure proxy */
    }
    else
    {
        data->point_count         = 0;
        data->points[0].touch_id  = 0;
        data->points[0].status    = POSIX_TOUCH_IDLE;
        data->points[0].x         = raw.x;
        data->points[0].y         = raw.y;
        data->points[0].pressure  = 0;
    }

    return (posix_ssize_t)sizeof(posix_touch_data_t);
}

static posix_ssize_t touch_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c;
    return POSIX_ERR_NOSUPP;
}

static int touch_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    (void)d;
    touch_file_t *file = (touch_file_t *)f;

    switch (cmd)
    {
    case POSIX_TOUCH_IOCTL_SET_CONFIG:
        file->cfg = *(posix_touch_config_t *)arg;
        return 0;

    case POSIX_TOUCH_IOCTL_GET_CONFIG:
        *(posix_touch_config_t *)arg = file->cfg;
        return 0;

    case POSIX_TOUCH_IOCTL_CALIBRATE:
        /* Calibration not required for CHSC6417; no-op */
        return 0;

    case POSIX_TOUCH_IOCTL_SET_POWER:
        {
            int on = *(int *)arg;
            (void)on;
            /* Power management not exposed by current Zephyr CHSC6417 driver */
            return 0;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- driver ops table ---------- */

const posix_driver_ops_t g_touch_ops =
{
    .open  = touch_open,
    .close = touch_close,
    .read  = touch_read,
    .write = touch_write,
    .ioctl = touch_ioctl,
};

/* ---------- auto-registration ---------- */

static int touch_init(void)
{
    /* Resolve the Zephyr device handle once at boot; never call DEVICE_DT_GET
     * inside read/ioctl paths so the app layer stays Zephyr-agnostic. */
    s_touch_drv0.zephyr_dev = DEVICE_DT_GET(DT_NODELABEL(touch_device));

    return posix_device_register("/dev/touch0", &g_touch_ops, &s_touch_drv0);
}
POSIX_INIT_DEVICE_EXPORT(touch_init);
