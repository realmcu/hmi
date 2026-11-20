#ifndef POSIX_IOCTL_TOUCH_H
#define POSIX_IOCTL_TOUCH_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* Touch state */
#define POSIX_TOUCH_IDLE    0
#define POSIX_TOUCH_PRESS   1
#define POSIX_TOUCH_RELEASE 2

/* Touch point data (multi-touch supported) */
typedef struct
{
    uint8_t  touch_id;          /* touch point ID */
    uint8_t  status;            /* PRESS/RELEASE/IDLE */
    uint16_t x;
    uint16_t y;
    uint16_t pressure;          /* pressure value */
} posix_touch_point_t;

/* Touch read result */
typedef struct
{
    uint8_t  point_count;       /* number of valid touch points */
    posix_touch_point_t points[5];  /* up to 5 points */
} posix_touch_data_t;

/* Upper-layer config: describes only the TP panel itself. The I2C slave address belongs to port-layer drv data,
 * does not appear in the upper-layer config surface -- to change the slave address, edit drv->i2c_addr in the port,
 * or go through /dev/i2cN's own ioctl. */
typedef struct
{
    uint16_t width;             /* TP resolution width */
    uint16_t height;            /* TP resolution height */
    uint8_t  swap_xy;           /* whether to swap XY */
} posix_touch_config_t;

/* ioctl commands */
#define POSIX_TOUCH_IOCTL_SET_CONFIG    POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 1)
#define POSIX_TOUCH_IOCTL_GET_CONFIG    POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 2)
#define POSIX_TOUCH_IOCTL_CALIBRATE     POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 3)
#define POSIX_TOUCH_IOCTL_SET_POWER     POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 4)
#define POSIX_TOUCH_IOCTL_GET_CHIP_ID   POSIX_IOC(POSIX_DEVICE_MAGIC_TOUCH, 5) /* arg: uint32_t * */

/* ================================================================
 * Touch binding API —— makes "apps only open /dev/touch0" possible
 *
 * Chip drivers .c (e.g. posix_port_cst816d.c / posix_port_chsc6417.c) register
 * /dev/cst816d and /dev/chsc6417 themselves via POSIX_INIT_DEVICE_EXPORT -- these
 * paths are always visible, convenient for debugging and direct access.
 *
 * The board port calls posix_touch_bind() at startup to decide "which chip on the
 * current board is the upper-layer's /dev/touch0". Binding means [alias registration]: look up the
 * registered chip path's ops+drv_data, then register the same set again under the alias name in the device table. Both paths share
 * the same chip's ops, so coordinates/state read are identical.
 *
 * Typical:
 *   posix_touch_bind("/dev/touch0", "/dev/cst816d");
 *   posix_fd_t tp = posix_open("/dev/touch0");
 *
 * Note:
 *  - chip_path must already be registered (chip driver's POSIX_INIT_DEVICE_EXPORT is at
 *    priority 1; the board port init calling bind should use POSIX_INIT_APP_EXPORT
 *    to guarantee ordering).
 *  - ref_count of alias and chip_path are independent. Opening both paths at once takes two
 *    fd slots and two chip file slots; touch use cases usually open only one, so no need to worry.
 *  - Returns POSIX_ERR_BUSY if alias already exists; returns POSIX_ERR_NODEV if chip_path
 *    is not registered.
 * ================================================================ */
int posix_touch_bind(const char *alias, const char *chip_path);
int posix_touch_unbind(const char *alias);

#endif /* POSIX_IOCTL_TOUCH_H */
