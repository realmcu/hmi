#ifndef POSIX_IOCTL_GSENSOR_H
#define POSIX_IOCTL_GSENSOR_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* Acceleration range */
#define POSIX_GSENSOR_RANGE_2G   0
#define POSIX_GSENSOR_RANGE_4G   1
#define POSIX_GSENSOR_RANGE_8G   2
#define POSIX_GSENSOR_RANGE_16G  3

/* Three-axis data */
typedef struct
{
    int32_t x;                  /* usually in mg (thousandths of g) */
    int32_t y;
    int32_t z;
} posix_gsensor_axis_t;

/* Config struct */
typedef struct
{
    uint8_t  range;             /* range 2G/4G/8G/16G */
    uint8_t  odr_hz;            /* output data rate (Hz) */
    uint8_t  low_power;         /* 0=normal, 1=low power */
    uint8_t  use_irq;           /* 1=enable DRDY interrupt; posix_read blocks until interrupted */
} posix_gsensor_config_t;

/* ioctl commands */
#define POSIX_GSENSOR_IOCTL_SET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 1)
#define POSIX_GSENSOR_IOCTL_GET_CONFIG     POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 2)
#define POSIX_GSENSOR_IOCTL_SET_POWER      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 3)
#define POSIX_GSENSOR_IOCTL_SELF_TEST      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 4)
#define POSIX_GSENSOR_IOCTL_READ_TEMP      POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 5)
/* Set interrupt wait timeout (ms); arg is uint32_t* */
#define POSIX_GSENSOR_IOCTL_SET_TIMEOUT    POSIX_IOC(POSIX_DEVICE_MAGIC_GSENSOR, 6)

/* ================================================================
 * Gsensor binding API —— symmetric with the touch layer; both are "device table aliases"
 *
 * Each chip driver .c registers its chip model path via POSIX_INIT_DEVICE_EXPORT
 * (/dev/sc7a20, and later /dev/bma253, /dev/lis3dh ...). The board port calls
 * posix_gsensor_bind() at startup to mount the board's chip as /dev/gsensor0.
 * Upper-layer apps only open /dev/gsensor0; for debugging you can open /dev/sc7a20 directly.
 *
 * The implementation is a typed thin wrapper over the framework's posix_device_bind_alias
 * (see posix_device.h).
 * ================================================================ */
int posix_gsensor_bind(const char *alias, const char *chip_path);
int posix_gsensor_unbind(const char *alias);

#endif /* POSIX_IOCTL_GSENSOR_H */
