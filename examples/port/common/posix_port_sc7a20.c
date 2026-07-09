/* ================================================================
 * G-sensor POSIX port (custom-rtos template)
 *
 * Reference chip: SC7A20 / SC7A20H (register-compatible with LIS2DH/LIS3DH)
 *
 * Key architecture: this driver does not access hardware directly, but goes through the POSIX framework
 *   /dev/i2c0   — I2C bus (posix_ioctl_i2c.h)
 *   /dev/gpio0/pXX — DRDY interrupt pin (posix_ioctl_gpio.h)
 * Two upstream devices handle all register R/W and interrupt subscription.
 *
 * Interrupt mode (cfg.use_irq=1):
 *   1. inside open: posix_open GPIO, register SET_IRQ callback gsensor_drdy_isr
 *   2. ISR calls posix_sem_give() to release DRDY semaphore
 *   3. posix_read waits for interrupt with posix_sem_take(timeout), then performs I2C read
 *
 * Polling mode (cfg.use_irq=0 default):
 *   posix_read directly issues I2C read without waiting for interrupt
 *
 * Actual SC7A20 direct-drive reference:
 *   board/evb/eBadge/app/driver/gsensor_sc7a20.[ch]
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_gsensor.h"
#include "ioctls/posix_ioctl_i2c.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <stdbool.h>
#include <string.h>

/* ---------------- SC7A20 registers ---------------- */
#define SC7A20_REG_WHO_AM_I     0x0F
#define SC7A20_REG_CTRL_REG1    0x20   /* ODR + LPen + Z/Y/X enable */
#define SC7A20_REG_CTRL_REG3    0x22   /* INT1 routing: I1_DRDY1 = bit4 */
#define SC7A20_REG_CTRL_REG4    0x23   /* BDU + FS + endianness */
#define SC7A20_REG_OUT_X_L      0x28

#define SC7A20_CHIP_ID          0x11

#define SC7A20_ADDR_HIGH        0x19
#define SC7A20_ADDR_LOW         0x18

/* SC7A20 sub-address auto-increment bit (bit7=1 enables multi-register burst read) */
#define SC7A20_AUTO_INC         0x80

/* CTRL_REG1 fields */
#define SC7A20_ODR_1HZ          (1u << 4)
#define SC7A20_ODR_10HZ         (2u << 4)
#define SC7A20_ODR_25HZ         (3u << 4)
#define SC7A20_ODR_50HZ         (4u << 4)
#define SC7A20_ODR_100HZ        (5u << 4)
#define SC7A20_ODR_200HZ        (6u << 4)
#define SC7A20_ODR_400HZ        (7u << 4)
#define SC7A20_LP_EN            (1u << 3)
#define SC7A20_XYZ_EN           0x07
#define SC7A20_POWERDOWN        0x00

/* CTRL_REG3 fields: DRDY1 interrupt output to INT1 pin */
#define SC7A20_I1_DRDY1         (1u << 4)

/* CTRL_REG4 fields */
#define SC7A20_BDU              (1u << 7)
#define SC7A20_FS_2G            (0u << 4)
#define SC7A20_FS_4G            (1u << 4)
#define SC7A20_FS_8G            (2u << 4)
#define SC7A20_FS_16G           (3u << 4)

/* mg/digit after raw>>6 for each range */
static const int s_sensitivity_mg[4] = { 4, 8, 16, 48 };

/* ---------------- driver private data ---------------- */
typedef struct
{
    int         unit;
    uint8_t     i2c_addr;        /* address hit by probe */
    uint8_t     probed;
    const char *i2c_path;        /* /dev/i2cX */
    const char *int_pin_path;    /* /dev/gpioY/pZZ; NULL = no interrupt pin */
} gsensor_drv_t;

typedef struct
{
    gsensor_drv_t         *drv;
    posix_gsensor_config_t cfg;
    posix_fd_t             i2c_fd;
    posix_fd_t             int_fd;       /* GPIO fd in interrupt mode */
    void                  *drdy_sem;     /* DRDY semaphore */
    uint32_t               drdy_timeout_ms;
} gsensor_file_t;

#define MAX_GSENSOR_FILES  2
static gsensor_file_t s_gsensor_files[MAX_GSENSOR_FILES];
static int            s_gsensor_file_used[MAX_GSENSOR_FILES];

/* ---------------- I2C register helpers (via posix /dev/i2cN) ---------------- */
static int gsensor_write_reg(gsensor_file_t *file, uint8_t reg, uint8_t val)
{
    posix_i2c_msg_t m =
    {
        .addr    = file->drv->i2c_addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = &val,
        .len     = 1,
    };
    return posix_ioctl(file->i2c_fd, POSIX_I2C_IOCTL_WRITE_REG, &m);
}

static int gsensor_read_regs(gsensor_file_t *file, uint8_t reg,
                             uint8_t *data, size_t len)
{
    posix_i2c_msg_t m =
    {
        .addr    = file->drv->i2c_addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = data,
        .len     = len,
    };
    return posix_ioctl(file->i2c_fd, POSIX_I2C_IOCTL_READ_REG, &m);
}

/* Probe 0x18 / 0x19, write back the address matching WHO_AM_I to drv->i2c_addr
 * (LOW first, because SA0 is tied to GND on eBadge board) */
static bool gsensor_probe(gsensor_file_t *file)
{
    gsensor_drv_t *drv = file->drv;
    const uint8_t  addrs[2] = { SC7A20_ADDR_LOW, SC7A20_ADDR_HIGH };
    for (uint8_t i = 0; i < 2; i++)
    {
        uint8_t id = 0;
        drv->i2c_addr = addrs[i];
        if (gsensor_read_regs(file, SC7A20_REG_WHO_AM_I, &id, 1) == POSIX_OK
            && id == SC7A20_CHIP_ID)
        {
            drv->probed = 1;
            return true;
        }
    }
    drv->i2c_addr = SC7A20_ADDR_LOW;
    drv->probed   = 0;
    return false;
}

/* ---------------- config translation ---------------- */
static uint8_t range_to_fs(uint8_t range)
{
    switch (range)
    {
    case POSIX_GSENSOR_RANGE_4G:  return SC7A20_FS_4G;
    case POSIX_GSENSOR_RANGE_8G:  return SC7A20_FS_8G;
    case POSIX_GSENSOR_RANGE_16G: return SC7A20_FS_16G;
    case POSIX_GSENSOR_RANGE_2G:
    default:                      return SC7A20_FS_2G;
    }
}

static uint8_t odr_to_field(uint8_t odr_hz)
{
    if (odr_hz >= 400) { return SC7A20_ODR_400HZ; }
    if (odr_hz >= 200) { return SC7A20_ODR_200HZ; }
    if (odr_hz >= 100) { return SC7A20_ODR_100HZ; }
    if (odr_hz >=  50) { return SC7A20_ODR_50HZ;  }
    if (odr_hz >=  25) { return SC7A20_ODR_25HZ;  }
    if (odr_hz >=  10) { return SC7A20_ODR_10HZ;  }
    return SC7A20_ODR_1HZ;
}

/* ---------------- DRDY interrupt callback (runs in ISR context) ---------------- */
static void gsensor_drdy_isr(void *arg)
{
    gsensor_file_t *file = (gsensor_file_t *)arg;
    /* posix_sem_give must be ISR-safe (see posix_device.h comment) */
    if (file && file->drdy_sem) { (void)posix_sem_give(file->drdy_sem); }
}

/* ---------------- write current cfg to chip + sync DRDY routing ---------------- */
static int gsensor_apply_config(gsensor_file_t *file)
{
    const posix_gsensor_config_t *cfg = &file->cfg;

    uint8_t ctrl1 = odr_to_field(cfg->odr_hz) | SC7A20_XYZ_EN;
    if (cfg->low_power) { ctrl1 |= SC7A20_LP_EN; }
    uint8_t ctrl3 = cfg->use_irq ? SC7A20_I1_DRDY1 : 0x00;
    uint8_t ctrl4 = SC7A20_BDU | range_to_fs(cfg->range);

    int r;
    if ((r = gsensor_write_reg(file, SC7A20_REG_CTRL_REG1, ctrl1)) != POSIX_OK) { return r; }
    if ((r = gsensor_write_reg(file, SC7A20_REG_CTRL_REG3, ctrl3)) != POSIX_OK) { return r; }
    if ((r = gsensor_write_reg(file, SC7A20_REG_CTRL_REG4, ctrl4)) != POSIX_OK) { return r; }
    return POSIX_OK;
}

/* ---------------- interrupt pin enable / disable ---------------- */
static int gsensor_setup_irq(gsensor_file_t *file)
{
    if (file->int_fd != POSIX_FD_NULL) { return POSIX_OK; }   /* already configured */
    if (!file->drv->int_pin_path)      { return POSIX_ERR_NOSUPP; }

    file->int_fd = posix_open(file->drv->int_pin_path);
    if (file->int_fd == POSIX_FD_NULL) { return POSIX_ERR_NODEV; }

    /* configure as input + pull-up */
    posix_gpio_config_t pin_cfg =
    {
        .pin = 0, .direction = POSIX_GPIO_DIR_INPUT, .pull = POSIX_GPIO_PULL_UP,
    };
    (void)posix_ioctl(file->int_fd, POSIX_GPIO_IOCTL_SET_DIR, &pin_cfg);

    /* create DRDY semaphore (max count 1, avoid edge stacking) */
    if (!file->drdy_sem)
    {
        file->drdy_sem = posix_sem_create("gsensor_drdy", 0, 1);
        if (!file->drdy_sem) { posix_close(file->int_fd); file->int_fd = POSIX_FD_NULL; return POSIX_ERR_NOMEM; }
    }

    /* register interrupt callback (rising edge; SC7A20 DRDY active-high by default) */
    posix_gpio_irq_t irq =
    {
        .pin = 0, .trigger = POSIX_GPIO_INT_RISING,
        .callback = gsensor_drdy_isr, .arg = file,
    };
    (void)posix_ioctl(file->int_fd, POSIX_GPIO_IOCTL_SET_IRQ, &irq);
    (void)posix_ioctl(file->int_fd, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL);
    return POSIX_OK;
}

static void gsensor_teardown_irq(gsensor_file_t *file)
{
    if (file->int_fd != POSIX_FD_NULL)
    {
        (void)posix_ioctl(file->int_fd, POSIX_GPIO_IOCTL_DISABLE_IRQ, NULL);
        posix_close(file->int_fd);
        file->int_fd = POSIX_FD_NULL;
    }
    if (file->drdy_sem)
    {
        posix_sem_delete(file->drdy_sem);
        file->drdy_sem = NULL;
    }
}

/* ---------------- POSIX driver interface ---------------- */
static void *gsensor_open(void *d, const char *p)
{
    (void)p;
    gsensor_drv_t  *drv = (gsensor_drv_t *)d;
    gsensor_file_t *f   = NULL;

    for (int i = 0; i < MAX_GSENSOR_FILES; i++)
    {
        if (!s_gsensor_file_used[i])
        {
            s_gsensor_file_used[i] = 1;
            f = &s_gsensor_files[i];
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    /* clear file struct (prevent stale data from recycled entry) */
    memset(f, 0, sizeof(*f));
    f->drv = drv;
    f->cfg.range     = POSIX_GSENSOR_RANGE_2G;
    f->cfg.odr_hz    = 100;
    f->cfg.low_power = 0;
    f->cfg.use_irq   = 0;
    f->drdy_timeout_ms = 1000;
    f->int_fd = POSIX_FD_NULL;

    /* open I2C bus */
    f->i2c_fd = posix_open(drv->i2c_path);
    if (f->i2c_fd == POSIX_FD_NULL)
    {
        s_gsensor_file_used[f - s_gsensor_files] = 0;
        return POSIX_OPEN_ERR;
    }

    /* set I2C speed (400kHz) */
    posix_i2c_config_t bus_cfg =
    {
        .speed_hz  = POSIX_I2C_SPEED_FAST,
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    (void)posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_SET_CONFIG, &bus_cfg);

    /* probe slave address + write default config */
    if (!drv->probed)
    {
        if (!gsensor_probe(f))
        {
            /* can not read WHO_AM_I at all, free resources and fail to avoid false OK on self-test/read */
            posix_close(f->i2c_fd);
            f->i2c_fd = POSIX_FD_NULL;
            s_gsensor_file_used[f - s_gsensor_files] = 0;
            return POSIX_OPEN_ERR;
        }
    }
    (void)gsensor_apply_config(f);
    return f;
}

static int gsensor_close(void *d, void *fv)
{
    (void)d;
    gsensor_file_t *f = (gsensor_file_t *)fv;
    if (!f) { return POSIX_OK; }

    /* power down: CTRL_REG1 = POWERDOWN */
    if (f->i2c_fd != POSIX_FD_NULL)
    {
        (void)gsensor_write_reg(f, SC7A20_REG_CTRL_REG1, SC7A20_POWERDOWN);
    }
    gsensor_teardown_irq(f);
    if (f->i2c_fd != POSIX_FD_NULL) { posix_close(f->i2c_fd); f->i2c_fd = POSIX_FD_NULL; }

    int idx = f - s_gsensor_files;
    if (idx >= 0 && idx < MAX_GSENSOR_FILES) { s_gsensor_file_used[idx] = 0; }
    return POSIX_OK;
}

/* posix_read = read 3-axis acceleration (in mg)
 * if use_irq=1, first block on DRDY semaphore, then I2C read */
static posix_ssize_t gsensor_read(void *d, void *fv, void *buf, size_t count)
{
    (void)d;
    gsensor_file_t *file = (gsensor_file_t *)fv;
    uint8_t raw[6];

    if (count < sizeof(posix_gsensor_axis_t)) { return POSIX_ERR_INVAL; }

    if (file->cfg.use_irq)
    {
        if (!file->drdy_sem)
        {
            /* app must first SET_CONFIG{use_irq=1} to create it; otherwise degrade to failure */
            return POSIX_ERR_NODEV;
        }
        if (posix_sem_take(file->drdy_sem, file->drdy_timeout_ms) != 0)
        {
            return POSIX_ERR_TIMEOUT;
        }
    }

    int r = gsensor_read_regs(file, SC7A20_REG_OUT_X_L | SC7A20_AUTO_INC, raw, 6);
    if (r != POSIX_OK) { return POSIX_ERR_IO; }

    int16_t rx = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
    int16_t ry = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
    int16_t rz = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);

    int sens = s_sensitivity_mg[file->cfg.range & 0x03];
    posix_gsensor_axis_t *axis = (posix_gsensor_axis_t *)buf;
    axis->x = ((int32_t)rx >> 6) * sens;
    axis->y = ((int32_t)ry >> 6) * sens;
    axis->z = ((int32_t)rz >> 6) * sens;
    return (posix_ssize_t)sizeof(posix_gsensor_axis_t);
}

static posix_ssize_t gsensor_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c;
    return POSIX_ERR_NOSUPP;
}

static int gsensor_ioctl(void *d, void *fv, unsigned long cmd, void *arg)
{
    (void)d;
    gsensor_file_t *file = (gsensor_file_t *)fv;

    switch (cmd)
    {
    case POSIX_GSENSOR_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gsensor_config_t new_cfg = *(posix_gsensor_config_t *)arg;
            uint8_t old_use_irq = file->cfg.use_irq;
            file->cfg = new_cfg;

            int r = gsensor_apply_config(file);
            if (r != POSIX_OK) { return r; }

            /* IRQ state transition */
            if (new_cfg.use_irq && !old_use_irq)
            {
                r = gsensor_setup_irq(file);
                if (r != POSIX_OK) { file->cfg.use_irq = 0; return r; }
            }
            else if (!new_cfg.use_irq && old_use_irq)
            {
                gsensor_teardown_irq(file);
            }
            return POSIX_OK;
        }

    case POSIX_GSENSOR_IOCTL_GET_CONFIG:
        if (!arg) { return POSIX_ERR_INVAL; }
        *(posix_gsensor_config_t *)arg = file->cfg;
        return POSIX_OK;

    case POSIX_GSENSOR_IOCTL_SET_POWER:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            int on = *(int *)arg;
            if (on)
            {
                return gsensor_apply_config(file);
            }
            return gsensor_write_reg(file, SC7A20_REG_CTRL_REG1, SC7A20_POWERDOWN);
        }

    case POSIX_GSENSOR_IOCTL_SELF_TEST:
        {
            uint8_t id = 0;
            if (gsensor_read_regs(file, SC7A20_REG_WHO_AM_I, &id, 1) != POSIX_OK)
            {
                return POSIX_ERR_IO;
            }
            return (id == SC7A20_CHIP_ID) ? POSIX_OK : POSIX_ERR_IO;
        }

    case POSIX_GSENSOR_IOCTL_READ_TEMP:
        /* SC7A20 has no dedicated temperature channel, keep template default */
        return POSIX_ERR_NOSUPP;

    case POSIX_GSENSOR_IOCTL_SET_TIMEOUT:
        if (!arg) { return POSIX_ERR_INVAL; }
        file->drdy_timeout_ms = *(uint32_t *)arg;
        return POSIX_OK;

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
    .unit         = 0,
    .i2c_addr     = SC7A20_ADDR_LOW,   /* eBadge board SA0 tied to GND -> 0x18 */
    .probed       = 0,
    .i2c_path     = "/dev/i2c0",
    .int_pin_path = NULL,   /* polling only; for DRDY interrupt, set "/dev/gpioX/pYY" */
};

/* ---------- auto-registration ----------
 *
 * 注册路径是芯片型号 /dev/sc7a20，方便调试直连；应用层看到的
 * "当前板 gsensor" 由 posix_port_gsensor.c 用 posix_gsensor_bind()
 * 挂成 /dev/gsensor0。未来加 BMA/LIS3DH 时各自注册自己的型号路径，
 * board 选型换一颗即可。
 */
static int gsensor_init(void)
{
    return posix_device_register("/dev/sc7a20", &g_gsensor_ops, &s_gsensor0);
}
POSIX_INIT_DEVICE_EXPORT(gsensor_init);
