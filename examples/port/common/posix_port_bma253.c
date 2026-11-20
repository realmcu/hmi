/* ================================================================
 * BMA253 gsensor POSIX port —— STUB / PLACEHOLDER
 *
 * This is a placeholder driver, intended to provide a copy-and-adapt
 * skeleton for "adding the Nth gsensor" in the future. By default it is [not compiled] — Kconfig
 *   CONFIG_POSIX_PORT_GSENSOR_BMA253=y
 * to be compiled in. Reference implementation: posix_port_sc7a20.c.
 *
 * ---- Structure is exactly symmetric with sc7a20 ----
 *   POSIX_INIT_DEVICE_EXPORT registers /dev/bma253 —— the chip model path
 *   mounted as /dev/gsensor0 via the board selection in posix_port_gsensor.c
 *
 * ---- Hardware facts (summary of the Bosch BMA253 datasheet) ----
 *   I2C address: SDO to GND -> 0x18; SDO to VDDIO -> 0x19 (coincidentally the same as SC7A20)
 *   CHIP_ID register 0x00 should read back 0xFA
 *   Range register 0x0F PMU_RANGE: 2G=0x03, 4G=0x05, 8G=0x08, 16G=0x0C
 *   Bandwidth register 0x10 PMU_BW: 7.81Hz~1000Hz steps
 *   Data registers 0x02..0x07, three axes low/high; each axis low byte bit0 is the new_data flag
 *   INT register 0x19 INT_EN_1 = 0x10 (data-ready), must be routed to 0x1A INT_MAP_1
 *
 * ---- Takeover checklist (required before testing real hardware) ----
 *   [ ] Confirm SDO pin wiring with a scope/multimeter, then set s_bma253_0.i2c_addr
 *   [ ] Confirm which GPIO INT1 is wired to and fill int_pin_path; use NULL without interrupts
 *   [ ] Confirm the I2C bus is i2c0 or i2c1 (gsensors are all on i2c0 in the current project)
 *   [ ] Finish implementing the TODO sections below; fill the range/ODR conversion tables from the datasheet
 *   [ ] Enable CONFIG_POSIX_PORT_GSENSOR_BMA253 in Kconfig
 *   [ ] Enable CONFIG_BOARD_GSENSOR_BMA253 in Kconfig (so posix_port_gsensor.c mounts it)
 *   [ ] Run `posix_gsensor poll /dev/bma253` for a direct check before switching the alias
 * ================================================================ */

#ifdef CONFIG_POSIX_PORT_GSENSOR_BMA253

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_gsensor.h"
#include "ioctls/posix_ioctl_i2c.h"
#include <stdbool.h>
#include <string.h>

/* ---------- BMA253 register map (datasheet §7) ---------- */
#define BMA253_ADDR_LOW     0x18   /* SDO -> GND */
#define BMA253_ADDR_HIGH    0x19   /* SDO -> VDDIO */
#define BMA253_REG_CHIP_ID  0x00
#define BMA253_CHIP_ID      0xFA

/* ---------- driver private data ---------- */
typedef struct
{
    int         unit;
    uint8_t     i2c_addr;
    uint8_t     probed;
    const char *i2c_path;
    const char *int_pin_path;
} bma253_drv_t;

typedef struct
{
    bma253_drv_t          *drv;
    posix_gsensor_config_t cfg;
    posix_fd_t             i2c_fd;
} bma253_file_t;

#define BMA253_MAX_FILES  1
static bma253_file_t s_bma253_files[BMA253_MAX_FILES];
static int           s_bma253_file_used[BMA253_MAX_FILES];

/* ---------- ops (all TODO; the current stub returns NOSUPP) ---------- */

static void *bma253_open(void *drv_data, const char *path)
{
    (void)path;
    /* TODO:
     *   1. allocate an unused slot from s_bma253_files
     *   2. posix_open(drv->i2c_path) to get the I2C fd
     *   3. probe: CHIP_ID should read 0xFA; otherwise return POSIX_OPEN_ERR
     *   4. if drv->int_pin_path is non-NULL, posix_open + SET_IRQ + create a semaphore
     */
    (void)drv_data;
    return POSIX_OPEN_ERR;   /* stub */
}

static int bma253_close(void *drv_data, void *file_priv)
{
    /* TODO: release I2C fd / GPIO fd / semaphore and mark the slot free */
    (void)drv_data; (void)file_priv;
    return POSIX_OK;
}

static posix_ssize_t bma253_read(void *drv_data, void *file_priv,
                                 void *buf, size_t count)
{
    /* TODO:
     *   IRQ mode: posix_sem_take(drdy) waits for the interrupt
     *   read six bytes 0x02..0x07 and assemble three int16 axes + shift per the datasheet
     *   convert to mg per sensitivity and fill posix_gsensor_axis_t
     */
    (void)drv_data; (void)file_priv; (void)buf; (void)count;
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t bma253_write(void *drv_data, void *file_priv,
                                  const void *buf, size_t count)
{
    (void)drv_data; (void)file_priv; (void)buf; (void)count;
    return POSIX_ERR_NOSUPP;
}

static int bma253_ioctl(void *drv_data, void *file_priv,
                        unsigned long cmd, void *arg)
{
    /* TODO: handle SET_CONFIG / GET_CONFIG / SET_POWER /
     *       SELF_TEST / READ_TEMP / SET_TIMEOUT */
    (void)drv_data; (void)file_priv; (void)cmd; (void)arg;
    return POSIX_ERR_NOSUPP;
}

static const posix_driver_ops_t g_bma253_ops =
{
    .open  = bma253_open,
    .close = bma253_close,
    .read  = bma253_read,
    .write = bma253_write,
    .ioctl = bma253_ioctl,
};

static bma253_drv_t s_bma253_0 =
{
    .unit         = 0,
    .i2c_addr     = BMA253_ADDR_LOW,   /* to be confirmed, see the takeover checklist */
    .probed       = 0,
    .i2c_path     = "/dev/i2c0",       /* to be confirmed */
    .int_pin_path = NULL,              /* polling mode; fill the path when INT1 is wired */
};

/* ---------- auto-registration ----------
 *
 * The registered path is the chip model /dev/bma253. The application layer sees
 * /dev/gsensor0 via the board selection in posix_port_gsensor.c and is unaware of model changes.
 */
static int bma253_init(void)
{
    return posix_device_register("/dev/bma253", &g_bma253_ops, &s_bma253_0);
}
POSIX_INIT_DEVICE_EXPORT(bma253_init);

#endif /* CONFIG_POSIX_PORT_GSENSOR_BMA253 */
