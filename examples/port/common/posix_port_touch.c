/* ================================================================
 * Touch 驱动 POSIX 端口（platform-independent）
 *
 * 参考芯片：CHSC6417（与 CST816S/CST826 寄存器布局兼容）
 *
 * 完全通过 posix 框架上游设备操作硬件：
 *   /dev/i2cN       — I²C 总线（posix_ioctl_i2c.h）
 *   /dev/gpioX/pYY  — INT 中断引脚（posix_ioctl_gpio.h，可选）
 *   /dev/gpioX/pZZ  — RST 复位引脚（posix_ioctl_gpio.h，可选）
 *
 * 中断模式（drv.int_pin_path != NULL）：
 *   posix_read 先 posix_sem_take() 等中断，再发 I²C 读
 * 轮询模式（int_pin_path == NULL）：
 *   posix_read 直接发 I²C 读
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_touch.h"
#include "ioctls/posix_ioctl_i2c.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <stdbool.h>
#include <string.h>

/* ---------- CHSC6417 寄存器 ---------- */
#define CHSC6417_REG_TOUCH_NUM   0x02   /* 触摸点数 */
#define CHSC6417_REG_POINT0      0x03   /* 第一点起始寄存器（4字节/点） */
#define CHSC6417_REG_GESTURE     0x01   /* 手势 */
#define CHSC6417_REG_CHIP_ID     0xA7   /* Chip ID */
#define CHSC6417_REG_POWER       0xA5   /* 电源控制 */

#define CHSC6417_CHIP_ID         0x17
#define CHSC6417_I2C_ADDR        0x2E
#define CHSC6417_MAX_POINTS      5

/* 触摸状态字节高 2 位 */
#define CHSC6417_TOUCH_DOWN      0x00
#define CHSC6417_TOUCH_UP        0x01

/* ---------- 驱动私有数据 ---------- */
typedef struct
{
    int         unit;
    uint8_t     i2c_addr;
    const char *i2c_path;
    const char *int_pin_path;   /* NULL = 轮询模式 */
    const char *rst_pin_path;   /* NULL = 不控制复位 */
} touch_drv_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct
{
    bool                  in_use;
    touch_drv_t          *drv;
    posix_touch_config_t  cfg;
    posix_fd_t            i2c_fd;
    posix_fd_t            int_fd;
    posix_fd_t            rst_fd;
    void                 *int_sem;
} touch_file_t;

#define MAX_TOUCH_FILES  2
static touch_file_t s_touch_files[MAX_TOUCH_FILES];

/* ---------- I²C 助手 ---------- */
static int touch_read_regs(touch_file_t *f, uint8_t reg, uint8_t *buf, size_t len)
{
    posix_i2c_msg_t m =
    {
        .addr    = f->drv->i2c_addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = buf,
        .len     = len,
    };
    return posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_READ_REG, &m);
}

static int touch_write_reg(touch_file_t *f, uint8_t reg, uint8_t val)
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
static void touch_int_isr(void *arg)
{
    touch_file_t *f = (touch_file_t *)arg;
    if (f && f->int_sem) { (void)posix_sem_give(f->int_sem); }
}

/* ---------- 复位芯片 ---------- */
static void touch_hw_reset(touch_file_t *f)
{
    if (f->rst_fd == POSIX_FD_NULL) { return; }

    posix_gpio_value_t v = { .value = 0 };
    posix_ioctl(f->rst_fd, POSIX_GPIO_IOCTL_SET_VALUE, &v);
    /* 保持低电平 ≥5ms（CHSC6417 datasheet 要求） */
    posix_port_delay_ms(10);
    v.value = 1;
    posix_ioctl(f->rst_fd, POSIX_GPIO_IOCTL_SET_VALUE, &v);
    posix_port_delay_ms(50);    /* 等待芯片启动 */
}

/* ---------- 建立 / 拆除 INT 中断 ---------- */
static int touch_setup_irq(touch_file_t *f)
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
        f->int_sem = posix_sem_create("touch_int", 0, 1);
        if (!f->int_sem)
        {
            posix_close(f->int_fd);
            f->int_fd = POSIX_FD_NULL;
            return POSIX_ERR_NOMEM;
        }
    }

    posix_gpio_irq_t irq =
    {
        .trigger  = POSIX_GPIO_INT_FALLING,   /* CHSC6417 INT 低有效 */
        .callback = touch_int_isr,
        .arg      = f,
    };
    posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_SET_IRQ, &irq);
    posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL);
    return POSIX_OK;
}

static void touch_teardown_irq(touch_file_t *f)
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
static void *touch_open(void *d, const char *p)
{
    (void)p;
    touch_drv_t  *drv = (touch_drv_t *)d;
    touch_file_t *f   = NULL;

    for (int i = 0; i < MAX_TOUCH_FILES; i++)
    {
        if (!s_touch_files[i].in_use)
        {
            f = &s_touch_files[i];
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

    /* 打开 I²C 总线 */
    f->i2c_fd = posix_open(drv->i2c_path);
    if (f->i2c_fd == POSIX_FD_NULL) { f->in_use = false; return POSIX_OPEN_ERR; }

    posix_i2c_config_t bus_cfg =
    {
        .speed_hz  = POSIX_I2C_SPEED_FAST,
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_SET_CONFIG, &bus_cfg);

    /* 打开 RST 引脚（如果有） */
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

    touch_hw_reset(f);

    /* 注册中断（有 int_pin_path 时）；失败降级为轮询 */
    (void)touch_setup_irq(f);

    return f;
}

/* ---------- close ---------- */
static int touch_close(void *d, void *fv)
{
    (void)d;
    touch_file_t *f = (touch_file_t *)fv;
    if (!f || !f->in_use) { return POSIX_OK; }

    touch_teardown_irq(f);
    if (f->rst_fd != POSIX_FD_NULL) { posix_close(f->rst_fd); f->rst_fd = POSIX_FD_NULL; }
    if (f->i2c_fd != POSIX_FD_NULL) { posix_close(f->i2c_fd); f->i2c_fd = POSIX_FD_NULL; }
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read：读触摸数据 ---------- */
static posix_ssize_t touch_read(void *d, void *fv, void *buf, size_t count)
{
    (void)d;
    touch_file_t *f = (touch_file_t *)fv;

    if (count < sizeof(posix_touch_data_t)) { return POSIX_ERR_INVAL; }

    /* 中断模式：等信号量（最多 100ms） */
    if (f->int_sem)
    {
        if (posix_sem_take(f->int_sem, 100) != 0) { return POSIX_ERR_TIMEOUT; }
    }

    posix_touch_data_t *data = (posix_touch_data_t *)buf;
    memset(data, 0, sizeof(*data));

    /* 读触摸点数 */
    uint8_t num = 0;
    if (touch_read_regs(f, CHSC6417_REG_TOUCH_NUM, &num, 1) != POSIX_OK)
    {
        return POSIX_ERR_IO;
    }
    num &= 0x0F;
    if (num > CHSC6417_MAX_POINTS) { num = CHSC6417_MAX_POINTS; }

    data->point_count = num;

    /* 每点 4 字节：[status_x_hi, x_lo, y_hi, y_lo] */
    for (uint8_t i = 0; i < num; i++)
    {
        uint8_t raw[4];
        uint8_t reg = CHSC6417_REG_POINT0 + i * 6;   /* CHSC6417 每点偏移 6 字节 */
        if (touch_read_regs(f, reg, raw, 4) != POSIX_OK) { break; }

        uint8_t  event = (raw[0] >> 6) & 0x03;
        uint16_t x     = ((uint16_t)(raw[0] & 0x0F) << 8) | raw[1];
        uint16_t y     = ((uint16_t)(raw[2] & 0x0F) << 8) | raw[3];

        if (f->cfg.swap_xy) { uint16_t tmp = x; x = y; y = tmp; }

        data->points[i].touch_id = i;
        data->points[i].x        = x;
        data->points[i].y        = y;
        data->points[i].pressure = (event == CHSC6417_TOUCH_DOWN) ? 1 : 0;
        data->points[i].status   = (event == CHSC6417_TOUCH_DOWN)
                                   ? POSIX_TOUCH_PRESS : POSIX_TOUCH_RELEASE;
    }

    return (posix_ssize_t)sizeof(posix_touch_data_t);
}

static posix_ssize_t touch_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c;
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int touch_ioctl(void *d, void *fv, unsigned long cmd, void *arg)
{
    (void)d;
    touch_file_t *f = (touch_file_t *)fv;

    switch (cmd)
    {
    case POSIX_TOUCH_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            f->cfg = *(posix_touch_config_t *)arg;
            f->drv->i2c_addr = f->cfg.i2c_addr;
            return POSIX_OK;
        }

    case POSIX_TOUCH_IOCTL_GET_CONFIG:
        if (!arg) { return POSIX_ERR_INVAL; }
        *(posix_touch_config_t *)arg = f->cfg;
        return POSIX_OK;

    case POSIX_TOUCH_IOCTL_CALIBRATE:
        /* CHSC6417 无需校准 */
        return POSIX_OK;

    case POSIX_TOUCH_IOCTL_SET_POWER:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            int on = *(int *)arg;
            /* 0x00 = normal，0x03 = sleep */
            return touch_write_reg(f, CHSC6417_REG_POWER, on ? 0x00 : 0x03);
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
const posix_driver_ops_t g_touch_ops =
{
    .open  = touch_open,
    .close = touch_close,
    .read  = touch_read,
    .write = touch_write,
    .ioctl = touch_ioctl,
};

/* ---------- 设备实例（移植时修改 i2c_path / int_pin_path / rst_pin_path） ---------- */
static touch_drv_t s_touch0 =
{
    .unit         = 0,
    .i2c_addr     = CHSC6417_I2C_ADDR,
    .i2c_path     = "/dev/i2c0",
    .int_pin_path = "/dev/gpio0/p5",    /* INT 引脚；NULL = 轮询模式 */
    .rst_pin_path = "/dev/gpio0/p6",    /* RST 引脚；NULL = 不控制复位 */
};

static int touch_init(void)
{
    return posix_device_register("/dev/touch0", &g_touch_ops, &s_touch0);
}
POSIX_INIT_DEVICE_EXPORT(touch_init);
