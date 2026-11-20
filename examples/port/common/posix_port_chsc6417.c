/* ================================================================
 * Touch driver POSIX port —— CHSC6417 (Chipsemi)
 *
 * Coexists with posix_port_cst816d.c; both files are compiled in by the CMake glob,
 * registering /dev/chsc6417 and /dev/cst816d respectively, so the upper layer opens whichever it needs.
 *
 * All hardware access goes through upstream posix framework devices:
 *   /dev/i2cN       — I2C bus (posix_ioctl_i2c.h)
 *   /dev/gpioX/pYY  — INT interrupt pin (posix_ioctl_gpio.h, optional)
 *   /dev/gpioX/pZZ  — RST reset pin (posix_ioctl_gpio.h, optional)
 *
 * Read protocol (from the Zephyr chsc6417 driver):
 *   first write the 4-byte address 0x2c000020, then read 8 bytes:
 *     output[1]   = touch point count
 *     output[2..] = point data, 5 bytes per point
 *       x = (x_h4 << 8) | x_l8
 *       y = (y_h4 << 8) | y_l8
 *       event == 0 = pressing (on CHSC6417 event=0 when pressed, event=1 when released)
 *
 * Interrupt mode (int_pin_path != NULL):
 *   INT falling edge -> ISR posix_sem_give -> posix_read waits on the semaphore then reads I2C
 * Polling mode (int_pin_path == NULL):
 *   posix_read issues the I2C read directly
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_touch.h"
#include "ioctls/posix_ioctl_i2c.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <stdbool.h>
#include <string.h>

/* ---------- CHSC6417 I2C read protocol constants ---------- */
#define CHSC6X_WRITE_ADDR    0x2c000020u
#define CHSC6X_WRITE_LEN     4
#define CHSC6X_READ_LEN      8
#define CHSC6X_EVENT_PRESS   0    /* on CHSC6417 event=0 when pressed, event=1 when released */

#define CHSC6417_I2C_ADDR    0x2E
#define CHSC6417_REG_POWER   0xA5   /* 0x00=normal, 0x03=sleep */
#define CHSC6417_REG_CHIP_ID 0xA7

/* ---------- Driver private data ---------- */
typedef struct
{
    int         unit;
    uint8_t     i2c_addr;
    const char *i2c_path;
    const char *int_pin_path;   /* NULL = polling mode */
    const char *rst_pin_path;   /* NULL = reset not controlled */
} chsc6417_drv_t;

/* ---------- per-open file private data ---------- */
typedef struct
{
    bool                  in_use;
    chsc6417_drv_t       *drv;
    posix_touch_config_t  cfg;
    posix_fd_t            i2c_fd;
    posix_fd_t            int_fd;
    posix_fd_t            rst_fd;
    void                 *int_sem;
} chsc6417_file_t;

#define CHSC6417_MAX_FILES  2
static chsc6417_file_t s_chsc6417_files[CHSC6417_MAX_FILES];

/* ---------- I2C read: write the 4-byte address first, then read 8 bytes ---------- */
static int chsc6417_read_raw(chsc6417_file_t *f, uint8_t out[CHSC6X_READ_LEN])
{
    uint32_t addr = CHSC6X_WRITE_ADDR;
    posix_i2c_msg_t m =
    {
        .addr    = f->drv->i2c_addr,   /* slave address is port-layer data */
        .reg     = 0,
        .reg_len = 0,
        .buf     = (uint8_t *) &addr,
        .len     = CHSC6X_WRITE_LEN,
    };
    if (posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_RAW_WRITE, &m) != POSIX_OK)
    {
        return POSIX_ERR_IO;
    }
    m.buf = out;
    m.len = CHSC6X_READ_LEN;
    if (posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_RAW_READ, &m) != POSIX_OK)
    {
        return POSIX_ERR_IO;
    }
    return POSIX_OK;
}

static int chsc6417_read_chip_id(chsc6417_file_t *f, uint32_t *chip_id)
{
    uint8_t id[4] = {0};
    posix_i2c_msg_t m =
    {
        .addr    = f->drv->i2c_addr,
        .reg     = CHSC6417_REG_CHIP_ID,
        .reg_len = 1,
        .buf     = id,
        .len     = sizeof(id),
    };
    int ret = posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_READ_REG, &m);

    if (ret == POSIX_OK)
    {
        *chip_id = ((uint32_t)id[0] << 24) | ((uint32_t)id[1] << 16) |
                   ((uint32_t)id[2] << 8) | id[3];
    }
    return ret;
}

/* ---------- I2C register write (power control, etc.) ---------- */
static int chsc6417_write_reg(chsc6417_file_t *f, uint8_t reg, uint8_t val)
{
    posix_i2c_msg_t m =
    {
        .addr    = f->drv->i2c_addr,   /* same as read_raw: slave address is port-layer */
        .reg     = reg,
        .reg_len = 1,
        .buf     = &val,
        .len     = 1,
    };
    return posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_WRITE_REG, &m);
}

/* ---------- INT pin ISR ---------- */
static void chsc6417_int_isr(void *arg)
{
    chsc6417_file_t *f = (chsc6417_file_t *)arg;
    if (f && f->int_sem) { (void)posix_sem_give(f->int_sem); }
}

/* ---------- Reset the chip (timing from the Zephyr driver) ---------- */
static void chsc6417_hw_reset(chsc6417_file_t *f)
{
    if (f->rst_fd == POSIX_FD_NULL) { return; }

    posix_gpio_value_t v = { .value = 1 };
    posix_ioctl(f->rst_fd, POSIX_GPIO_IOCTL_SET_VALUE, &v);
    posix_port_delay_ms(10);
    v.value = 0;
    posix_ioctl(f->rst_fd, POSIX_GPIO_IOCTL_SET_VALUE, &v);
    posix_port_delay_ms(10);
    v.value = 1;
    posix_ioctl(f->rst_fd, POSIX_GPIO_IOCTL_SET_VALUE, &v);
    posix_port_delay_ms(50);
}

/* ---------- Set up / tear down the INT interrupt ---------- */
static int chsc6417_setup_irq(chsc6417_file_t *f)
{
    if (f->int_fd != POSIX_FD_NULL) { return POSIX_OK; }
    if (!f->drv->int_pin_path)      { return POSIX_ERR_NOSUPP; }

    f->int_fd = posix_open(f->drv->int_pin_path);
    if (f->int_fd == POSIX_FD_NULL) { return POSIX_ERR_NODEV; }

    posix_gpio_config_t pin_cfg =
    {
        .direction = POSIX_GPIO_DIR_INPUT,
        .pull      = POSIX_GPIO_PULL_UP,
    };
    posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_SET_DIR, &pin_cfg);

    if (!f->int_sem)
    {
        f->int_sem = posix_sem_create("chsc6417_int", 0, 1);
        if (!f->int_sem)
        {
            posix_close(f->int_fd);
            f->int_fd = POSIX_FD_NULL;
            return POSIX_ERR_NOMEM;
        }
    }

    posix_gpio_irq_t irq =
    {
        .trigger  = POSIX_GPIO_INT_FALLING,
        .callback = chsc6417_int_isr,
        .arg      = f,
    };
    posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_SET_IRQ, &irq);
    posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL);
    return POSIX_OK;
}

static void chsc6417_teardown_irq(chsc6417_file_t *f)
{
    if (f->int_fd != POSIX_FD_NULL)
    {
        posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_DISABLE_IRQ, NULL);
        posix_close(f->int_fd);
        f->int_fd = POSIX_FD_NULL;
    }
    if (f->int_sem)
    {
        posix_sem_delete(f->int_sem);
        f->int_sem = NULL;
    }
}

/* ---------- open ---------- */
static void *chsc6417_open(void *d, const char *p)
{
    (void)p;
    chsc6417_drv_t  *drv = (chsc6417_drv_t *)d;
    chsc6417_file_t *f   = NULL;

    for (int i = 0; i < CHSC6417_MAX_FILES; i++)
    {
        if (!s_chsc6417_files[i].in_use)
        {
            f = &s_chsc6417_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use = true;
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv    = drv;
    f->int_fd = POSIX_FD_NULL;
    f->rst_fd = POSIX_FD_NULL;

    f->i2c_fd = posix_open(drv->i2c_path);
    if (f->i2c_fd == POSIX_FD_NULL) { f->in_use = false; return POSIX_OPEN_ERR; }

    posix_i2c_config_t bus_cfg =
    {
        .speed_hz  = POSIX_I2C_SPEED_FAST,
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_SET_CONFIG, &bus_cfg);

    if (drv->rst_pin_path)
    {
        f->rst_fd = posix_open(drv->rst_pin_path);
        if (f->rst_fd != POSIX_FD_NULL)
        {
            posix_gpio_config_t rst_cfg =
            {
                .direction     = POSIX_GPIO_DIR_OUTPUT,
                .pull          = POSIX_GPIO_PULL_NONE,
                .initial_value = 1,
            };
            posix_ioctl(f->rst_fd, POSIX_GPIO_IOCTL_SET_DIR, &rst_cfg);
        }
    }

    chsc6417_hw_reset(f);
    (void)chsc6417_setup_irq(f);
    return f;
}

/* ---------- close ---------- */
static int chsc6417_close(void *d, void *fv)
{
    (void)d;
    chsc6417_file_t *f = (chsc6417_file_t *)fv;
    if (!f || !f->in_use) { return POSIX_OK; }

    chsc6417_teardown_irq(f);
    if (f->rst_fd != POSIX_FD_NULL) { posix_close(f->rst_fd); f->rst_fd = POSIX_FD_NULL; }
    if (f->i2c_fd != POSIX_FD_NULL) { posix_close(f->i2c_fd); f->i2c_fd = POSIX_FD_NULL; }
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read: read one touch snapshot ---------- */
static posix_ssize_t chsc6417_read(void *d, void *fv, void *buf, size_t count)
{
    (void)d;
    chsc6417_file_t *f = (chsc6417_file_t *)fv;

    if (count < sizeof(posix_touch_data_t)) { return POSIX_ERR_INVAL; }

    /* Interrupt mode: wait on the semaphore (100ms timeout) */
    if (f->int_sem)
    {
        if (posix_sem_take(f->int_sem, 100) != 0) { return POSIX_ERR_TIMEOUT; }
    }

    uint8_t raw[CHSC6X_READ_LEN];
    if (chsc6417_read_raw(f, raw) != POSIX_OK) { return POSIX_ERR_IO; }

    posix_touch_data_t *data = (posix_touch_data_t *)buf;
    memset(data, 0, sizeof(*data));

    /* A valid frame should have header 0xff; otherwise treat it as no data (possibly bus contention) */
    if (raw[0] != 0xff) { return (posix_ssize_t)sizeof(posix_touch_data_t); }

    uint8_t point_num = raw[1];
    if (point_num == 0) { return (posix_ssize_t)sizeof(posix_touch_data_t); }
    if (point_num > 1)  { point_num = 1; }   /* CHSC6417 is single-point */

    /* Parse point data (starting at output[2], 5 bytes per point) */
    uint8_t *p    = &raw[2];
    uint8_t  x_l8 = p[0];
    uint8_t  y_l8 = p[1];
    uint8_t  x_h4 = p[3] & 0x0F;
    uint8_t  y_h4 = (p[3] >> 4) & 0x0F;
    uint8_t  event = p[4] & 0x0F;

    uint16_t x = ((uint16_t)x_h4 << 8) | x_l8;
    uint16_t y = ((uint16_t)y_h4 << 8) | y_l8;

    if (f->cfg.swap_xy) { uint16_t tmp = x; x = y; y = tmp; }

    data->point_count        = point_num;
    data->points[0].touch_id = 0;
    data->points[0].x        = x;
    data->points[0].y        = y;
    data->points[0].pressure = (event == CHSC6X_EVENT_PRESS) ? 1 : 0;
    data->points[0].status   = (event == CHSC6X_EVENT_PRESS)
                               ? POSIX_TOUCH_PRESS : POSIX_TOUCH_RELEASE;

    return (posix_ssize_t)sizeof(posix_touch_data_t);
}

static posix_ssize_t chsc6417_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c;
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int chsc6417_ioctl(void *d, void *fv, unsigned long cmd, void *arg)
{
    (void)d;
    chsc6417_file_t *f = (chsc6417_file_t *)fv;

    switch (cmd)
    {
    case POSIX_TOUCH_IOCTL_SET_CONFIG:
        if (!arg) { return POSIX_ERR_INVAL; }
        f->cfg = *(posix_touch_config_t *)arg;
        return POSIX_OK;

    case POSIX_TOUCH_IOCTL_GET_CONFIG:
        if (!arg) { return POSIX_ERR_INVAL; }
        *(posix_touch_config_t *)arg = f->cfg;
        return POSIX_OK;

    case POSIX_TOUCH_IOCTL_GET_CHIP_ID:
        if (!arg) { return POSIX_ERR_INVAL; }
        return chsc6417_read_chip_id(f, (uint32_t *)arg);

    case POSIX_TOUCH_IOCTL_CALIBRATE:
        return POSIX_OK;

    case POSIX_TOUCH_IOCTL_SET_POWER:
        if (!arg) { return POSIX_ERR_INVAL; }
        return chsc6417_write_reg(f, CHSC6417_REG_POWER,
                                  *(int *)arg ? 0x00 : 0x03);

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- Driver ops table ---------- */
static const posix_driver_ops_t g_chsc6417_ops =
{
    .open  = chsc6417_open,
    .close = chsc6417_close,
    .read  = chsc6417_read,
    .write = chsc6417_write,
    .ioctl = chsc6417_ioctl,
};

/* ---------- Device instance
 * i2c1 is enabled in the overlay (SCL=P4_4, SDA=P4_3)
 * TP_INT -> P0_0 -> /dev/gpio0/p0
 * TP_RST -> P0_3 -> /dev/gpio0/p3
 * ---------------------------------------------------------------- */
static chsc6417_drv_t s_chsc6417_0 =
{
    .unit         = 0,
    .i2c_addr     = CHSC6417_I2C_ADDR,
    .i2c_path     = "/dev/i2c1",
    .int_pin_path = "/dev/gpio0/p0",
    .rst_pin_path = "/dev/gpio0/p3",
};

static int chsc6417_init(void)
{
    return posix_device_register("/dev/chsc6417", &g_chsc6417_ops, &s_chsc6417_0);
}
POSIX_INIT_DEVICE_EXPORT(chsc6417_init);
