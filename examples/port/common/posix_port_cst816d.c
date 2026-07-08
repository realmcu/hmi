/* ================================================================
 * Touch 驱动 POSIX 端口 —— CST816D（Hynitron）
 *
 * 与 posix_port_chsc6417.c 并列存在；两份文件都会被 CMake glob 编入，
 * 分别注册 /dev/chsc6417 与 /dev/cst816d，上层按需 open 其中之一。
 *
 * 参考实现：
 *   Zephyr 上游驱动：
 *     modules/display/device/zephyr/touch/8773G/touch_cst816d_zephyr.c
 *   Zephyr binding：
 *     modules/display/device/zephyr/dts/bindings/hynitron,cst816d.yaml
 *
 * 完全通过 posix 框架上游设备操作硬件：
 *   /dev/i2cN       — I2C 总线（posix_ioctl_i2c.h）
 *   /dev/gpioX/pYY  — INT 中断引脚（posix_ioctl_gpio.h，可选）
 *   /dev/gpioX/pZZ  — RST 复位引脚（posix_ioctl_gpio.h，可选）
 *
 * 读取协议（来自 Zephyr cst816d 驱动）：
 *   写 1 字节 reg 0x00，再读 24 字节：
 *     data[3] 高 2 位 == 2  -> 按下
 *     x = ((data[3] & 0x0F) << 8) | data[4]
 *     y = ((data[5] & 0x0F) << 8) | data[6]
 *
 * 中断模式（int_pin_path != NULL）：
 *   INT 下降沿 -> ISR posix_sem_give -> posix_read 等信号量后读 I2C
 * 轮询模式（int_pin_path == NULL）：
 *   posix_read 直接发 I2C 读
 *
 * 手势释放超时：
 *   CST816D 在整个按压过程只会触发一次 INT，因此依赖软件超时
 *   （对齐 zephyr 上游 touch_gesture_release_timer）在 release_ms
 *   后把状态置为释放。这里用 posix_sem_take(release_ms) 直接表达。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_touch.h"
#include "ioctls/posix_ioctl_i2c.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <stdbool.h>
#include <string.h>

/* ---------- CST816D I2C 读取协议常量 ---------- */
#define CST816D_REG_DATA             0x00
#define CST816D_READ_LEN             24
#define CST816D_EVENT_MASK           0xC0   /* data[3] 高 2 位 */
#define CST816D_EVENT_PRESS_SHIFT    6
#define CST816D_EVENT_PRESS          2      /* pressing 时 (data[3] >> 6) == 2 */

#define CST816D_I2C_ADDR             0x15   /* 上游 dts 通用地址 */
#define CST816D_REG_POWER            0xA5   /* 0x00=normal, 0x03=sleep */

#define CST816D_DEFAULT_RELEASE_MS   30     /* 与 zephyr 驱动 CST816D_DEFAULT_TIMEOUT_MS 一致 */

/* ---------- 驱动私有数据 ---------- */
typedef struct
{
    int         unit;
    uint8_t     i2c_addr;
    const char *i2c_path;
    const char *int_pin_path;   /* NULL = 轮询模式 */
    const char *rst_pin_path;   /* NULL = 不控制复位 */
    uint32_t    release_ms;     /* 手势释放超时 */
} cst816d_drv_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct
{
    bool                  in_use;
    cst816d_drv_t        *drv;
    posix_touch_config_t  cfg;
    posix_fd_t            i2c_fd;
    posix_fd_t            int_fd;
    posix_fd_t            rst_fd;
    void                 *int_sem;
} cst816d_file_t;

#define CST816D_MAX_FILES  2
static cst816d_file_t s_cst816d_files[CST816D_MAX_FILES];

/* ---------- I2C 读取：写 reg 0x00，再读 24 字节 ---------- */
static int cst816d_read_raw(cst816d_file_t *f, uint8_t out[CST816D_READ_LEN])
{
    uint8_t reg = CST816D_REG_DATA;
    posix_i2c_msg_t m =
    {
        .addr    = f->drv->i2c_addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = out,
        .len     = CST816D_READ_LEN,
    };
    /* 优先使用 write_read 组合，避免 START 之间被打断；框架里对应
     * WRITE_REG 的读回路径（reg_len=1，len=CST816D_READ_LEN）。 */
    return posix_ioctl(f->i2c_fd, POSIX_I2C_IOCTL_READ_REG, &m);
}

/* ---------- I2C 寄存器写（电源控制等） ---------- */
static int cst816d_write_reg(cst816d_file_t *f, uint8_t reg, uint8_t val)
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
static void cst816d_int_isr(void *arg)
{
    cst816d_file_t *f = (cst816d_file_t *)arg;
    if (f && f->int_sem) { (void)posix_sem_give(f->int_sem); }
}

/* ---------- 复位芯片（时序对齐 zephyr cst816d_init） ---------- */
static void cst816d_hw_reset(cst816d_file_t *f)
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
    posix_port_delay_ms(100);          /* zephyr 用 100ms，比 CHSC6417 稍长 */
}

/* ---------- 建立 / 拆除 INT 中断 ---------- */
static int cst816d_setup_irq(cst816d_file_t *f)
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
        f->int_sem = posix_sem_create("cst816d_int", 0, 1);
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
        .callback = cst816d_int_isr,
        .arg      = f,
    };
    posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_SET_IRQ, &irq);
    posix_ioctl(f->int_fd, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL);
    return POSIX_OK;
}

static void cst816d_teardown_irq(cst816d_file_t *f)
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
static void *cst816d_open(void *d, const char *p)
{
    (void)p;
    cst816d_drv_t  *drv = (cst816d_drv_t *)d;
    cst816d_file_t *f   = NULL;

    for (int i = 0; i < CST816D_MAX_FILES; i++)
    {
        if (!s_cst816d_files[i].in_use)
        {
            f = &s_cst816d_files[i];
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
        .speed_hz  = POSIX_I2C_SPEED_FAST,   /* 400kHz，对齐 overlay */
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

    cst816d_hw_reset(f);
    (void)cst816d_setup_irq(f);
    return f;
}

/* ---------- close ---------- */
static int cst816d_close(void *d, void *fv)
{
    (void)d;
    cst816d_file_t *f = (cst816d_file_t *)fv;
    if (!f || !f->in_use) { return POSIX_OK; }

    cst816d_teardown_irq(f);
    if (f->rst_fd != POSIX_FD_NULL) { posix_close(f->rst_fd); f->rst_fd = POSIX_FD_NULL; }
    if (f->i2c_fd != POSIX_FD_NULL) { posix_close(f->i2c_fd); f->i2c_fd = POSIX_FD_NULL; }
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read：读一次触摸快照 ----------
 *
 * CST816D 特性：一次按压只产生 1 次 INT。语义参照 zephyr 上游驱动的
 * touch_gesture_release_timer——中断后若在 release_ms 内没有下一次中断，
 * 就视作释放。这里直接用 posix_sem_take(release_ms) 表达该超时：
 *   - 拿到信号量 -> 读 I2C，按数据返回 press/release
 *   - 超时      -> 返回 release
 * 轮询模式（无 INT）：不带超时地立即读一次。
 */
static posix_ssize_t cst816d_read(void *d, void *fv, void *buf, size_t count)
{
    (void)d;
    cst816d_file_t *f = (cst816d_file_t *)fv;

    if (count < sizeof(posix_touch_data_t)) { return POSIX_ERR_INVAL; }

    posix_touch_data_t *data = (posix_touch_data_t *)buf;
    memset(data, 0, sizeof(*data));

    if (f->int_sem)
    {
        if (posix_sem_take(f->int_sem, f->drv->release_ms) != 0)
        {
            /* 超过 release_ms 没有新的 INT —— 视为释放 */
            data->point_count      = 0;
            data->points[0].status = POSIX_TOUCH_RELEASE;
            return (posix_ssize_t)sizeof(posix_touch_data_t);
        }
    }

    uint8_t raw[CST816D_READ_LEN];
    if (cst816d_read_raw(f, raw) != POSIX_OK) { return POSIX_ERR_IO; }

    bool pressing = ((raw[3] >> CST816D_EVENT_PRESS_SHIFT) == CST816D_EVENT_PRESS);
    uint16_t x = ((uint16_t)(raw[3] & 0x0F) << 8) | raw[4];
    uint16_t y = ((uint16_t)(raw[5] & 0x0F) << 8) | raw[6];

    if (f->cfg.swap_xy) { uint16_t tmp = x; x = y; y = tmp; }

    if (pressing)
    {
        data->point_count        = 1;
        data->points[0].touch_id = 0;
        data->points[0].x        = x;
        data->points[0].y        = y;
        data->points[0].pressure = 1;
        data->points[0].status   = POSIX_TOUCH_PRESS;
    }
    else
    {
        data->point_count      = 0;
        data->points[0].status = POSIX_TOUCH_RELEASE;
    }

    return (posix_ssize_t)sizeof(posix_touch_data_t);
}

static posix_ssize_t cst816d_write(void *d, void *f, const void *b, size_t c)
{
    (void)d; (void)f; (void)b; (void)c;
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int cst816d_ioctl(void *d, void *fv, unsigned long cmd, void *arg)
{
    (void)d;
    cst816d_file_t *f = (cst816d_file_t *)fv;

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
        return cst816d_write_reg(f, CST816D_REG_POWER,
                                 *(int *)arg ? 0x00 : 0x03);

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_cst816d_ops =
{
    .open  = cst816d_open,
    .close = cst816d_close,
    .read  = cst816d_read,
    .write = cst816d_write,
    .ioctl = cst816d_ioctl,
};

/* ---------- 设备实例
 * i2c1  已在 overlay enabled（SCL=P4_4, SDA=P4_3, 400kHz）
 * TP_INT -> P0_0 -> /dev/gpio0/p0
 * TP_RST -> P0_3 -> /dev/gpio0/p3
 * ---------------------------------------------------------------- */
static cst816d_drv_t s_cst816d_0 =
{
    .unit         = 0,
    .i2c_addr     = CST816D_I2C_ADDR,
    .i2c_path     = "/dev/i2c1",
    .int_pin_path = "/dev/gpio0/p0",     /* TP_INT = P0_0 */
    .rst_pin_path = "/dev/gpio0/p3",     /* TP_RST = P0_3 */
    .release_ms   = CST816D_DEFAULT_RELEASE_MS,
};

static int cst816d_init(void)
{
    return posix_device_register("/dev/cst816d", &g_cst816d_ops, &s_cst816d_0);
}
POSIX_INIT_DEVICE_EXPORT(cst816d_init);
