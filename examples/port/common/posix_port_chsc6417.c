/* ================================================================
 * Touch 驱动 POSIX 端口 —— CHSC6417（Chipsemi）
 *
 * 与 posix_port_cst816d.c 并列存在；两份文件都会被 CMake glob 编入，
 * 分别注册 /dev/chsc6417 与 /dev/cst816d，上层按需 open 其中之一。
 *
 * 完全通过 posix 框架上游设备操作硬件：
 *   /dev/i2cN       — I2C 总线（posix_ioctl_i2c.h）
 *   /dev/gpioX/pYY  — INT 中断引脚（posix_ioctl_gpio.h，可选）
 *   /dev/gpioX/pZZ  — RST 复位引脚（posix_ioctl_gpio.h，可选）
 *
 * 读取协议（来自 Zephyr chsc6417 驱动）：
 *   先写 4 字节地址 0x2c000020，再读 8 字节：
 *     output[1]   = 触摸点数
 *     output[2..] = 点数据，每点 5 字节
 *       x = (x_h4 << 8) | x_l8
 *       y = (y_h4 << 8) | y_l8
 *       event == 0 = pressing（CHSC6417 按下时 event=0，抬起时 event=1）
 *
 * 中断模式（int_pin_path != NULL）：
 *   INT 下降沿 -> ISR posix_sem_give -> posix_read 等信号量后读 I2C
 * 轮询模式（int_pin_path == NULL）：
 *   posix_read 直接发 I2C 读
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_touch.h"
#include "ioctls/posix_ioctl_i2c.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <stdbool.h>
#include <string.h>

/* ---------- CHSC6417 I2C 读取协议常量 ---------- */
#define CHSC6X_WRITE_ADDR    0x2c000020u
#define CHSC6X_WRITE_LEN     4
#define CHSC6X_READ_LEN      8
#define CHSC6X_EVENT_PRESS   0    /* CHSC6417 按下时 event=0，抬起时 event=1 */

#define CHSC6417_I2C_ADDR    0x2E
#define CHSC6417_REG_POWER   0xA5   /* 0x00=normal, 0x03=sleep */

/* ---------- 驱动私有数据 ---------- */
typedef struct
{
    int         unit;
    uint8_t     i2c_addr;
    const char *i2c_path;
    const char *int_pin_path;   /* NULL = 轮询模式 */
    const char *rst_pin_path;   /* NULL = 不控制复位 */
} chsc6417_drv_t;

/* ---------- per-open 文件私有数据 ---------- */
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

/* ---------- I2C 读取：先写 4 字节地址，再读 8 字节 ---------- */
static int chsc6417_read_raw(chsc6417_file_t *f, uint8_t out[CHSC6X_READ_LEN])
{
    uint32_t addr = CHSC6X_WRITE_ADDR;
    posix_i2c_msg_t m =
    {
        .addr    = f->drv->i2c_addr,
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

/* ---------- I2C 寄存器写（电源控制等） ---------- */
static int chsc6417_write_reg(chsc6417_file_t *f, uint8_t reg, uint8_t val)
{
    posix_i2c_msg_t m =
    {
        .addr    = f->drv->i2c_addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = &val,
        .len     = 1,
    };
    return posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_WRITE_REG, &m);
}

/* ---------- INT 引脚 ISR ---------- */
static void chsc6417_int_isr(void *arg)
{
    chsc6417_file_t *f = (chsc6417_file_t *)arg;
    if (f && f->int_sem) { (void)posix_sem_give(f->int_sem); }
}

/* ---------- 复位芯片（参考 Zephyr 驱动时序） ---------- */
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

/* ---------- 建立 / 拆除 INT 中断 ---------- */
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
    f->cfg.i2c_addr = drv->i2c_addr;

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

/* ---------- read：读一次触摸快照 ---------- */
static posix_ssize_t chsc6417_read(void *d, void *fv, void *buf, size_t count)
{
    (void)d;
    chsc6417_file_t *f = (chsc6417_file_t *)fv;

    if (count < sizeof(posix_touch_data_t)) { return POSIX_ERR_INVAL; }

    /* 中断模式：等信号量（100ms 超时） */
    if (f->int_sem)
    {
        if (posix_sem_take(f->int_sem, 100) != 0) { return POSIX_ERR_TIMEOUT; }
    }

    uint8_t raw[CHSC6X_READ_LEN];
    if (chsc6417_read_raw(f, raw) != POSIX_OK) { return POSIX_ERR_IO; }

    posix_touch_data_t *data = (posix_touch_data_t *)buf;
    memset(data, 0, sizeof(*data));

    /* 有效帧的 header 应为 0xff；否则视为无数据（可能是总线竞争污染） */
    if (raw[0] != 0xff) { return (posix_ssize_t)sizeof(posix_touch_data_t); }

    uint8_t point_num = raw[1];
    if (point_num == 0) { return (posix_ssize_t)sizeof(posix_touch_data_t); }
    if (point_num > 1)  { point_num = 1; }   /* CHSC6417 单点 */

    /* 解析点数据（从 output[2] 开始，每点 5 字节） */
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
        f->drv->i2c_addr = f->cfg.i2c_addr;
        return POSIX_OK;

    case POSIX_TOUCH_IOCTL_GET_CONFIG:
        if (!arg) { return POSIX_ERR_INVAL; }
        *(posix_touch_config_t *)arg = f->cfg;
        return POSIX_OK;

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

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_chsc6417_ops =
{
    .open  = chsc6417_open,
    .close = chsc6417_close,
    .read  = chsc6417_read,
    .write = chsc6417_write,
    .ioctl = chsc6417_ioctl,
};

/* ---------- 设备实例
 * i2c1  已在 overlay enabled（SCL=P4_4, SDA=P4_3）
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
