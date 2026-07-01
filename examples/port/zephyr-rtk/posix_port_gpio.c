/* ================================================================
 * GPIO 驱动 — 基于 Zephyr GPIO API 的 posix-device 适配层
 *
 * 路径格式: /dev/gpio<N>/p<Pin>
 *   /dev/gpio0/pXX  → gpioa，pin XX
 *   /dev/gpio1/pXX  → gpiob，pin XX
 *
 * 依赖：DTS 中 gpioa / gpiob 节点已 enabled，且 CONFIG_GPIO=y。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gpio.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>

/* ---------- 控制器私有数据（每个 posix 设备一份） ---------- */
typedef struct
{
    const struct device *dev;   /* Zephyr GPIO device */
} gpio_drv_data_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct
{
    bool              in_use;
    gpio_drv_data_t  *drv;
    gpio_pin_t        pin;
    uint8_t           direction;
    uint8_t           pull;

    /* 中断相关 */
    struct gpio_callback zephyr_cb;          /* Zephyr callback 节点 */
    void (*user_cb)(void *arg);
    void             *user_arg;
    gpio_flags_t      int_flags;             /* SET_IRQ 时存的触发模式 */
} gpio_file_t;

#define MAX_GPIO_FILES  16
static gpio_file_t s_gpio_files[MAX_GPIO_FILES];

/* ---------- Zephyr 中断 dispatcher ---------- */
static void gpio_zephyr_isr(const struct device *port,
                            struct gpio_callback *cb,
                            gpio_port_pins_t pins)
{
    (void)port;
    (void)pins;
    gpio_file_t *f = CONTAINER_OF(cb, gpio_file_t, zephyr_cb);
    if (f->user_cb)
    {
        f->user_cb(f->user_arg);
    }
}

/* ---------- open：解析路径，绑定 pin ---------- */
static void *gpio_open(void *drv_data, const char *path)
{
    gpio_drv_data_t *d = (gpio_drv_data_t *)drv_data;

    /* 解析 pin 号：找 "/p" 后面的数字，兼容 /pN 和 /pinN */
    const char *p = strstr(path, "/p");
    if (!p) { return POSIX_OPEN_ERR; }
    p += 2;                             /* 跳过 "/p" */
    if (p[0] == 'i' && p[1] == 'n') { p += 2; }  /* 跳过 "in" */

    char *end;
    long pin = strtol(p, &end, 10);
    if (end == p || *end != '\0' || pin < 0 || pin > 31)
    {
        return POSIX_OPEN_ERR;
    }

    if (!device_is_ready(d->dev)) { return POSIX_OPEN_ERR; }

    gpio_file_t *f = NULL;
    for (int i = 0; i < MAX_GPIO_FILES; i++)
    {
        if (!s_gpio_files[i].in_use)
        {
            f = &s_gpio_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use = true;
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv       = d;
    f->pin       = (gpio_pin_t)pin;
    f->direction = POSIX_GPIO_DIR_INPUT;
    f->pull      = POSIX_GPIO_PULL_NONE;
    return f;
}

/* ---------- close ---------- */
static int gpio_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    gpio_file_t *f = (gpio_file_t *)file_priv;
    if (!f || !f->in_use) { return POSIX_OK; }

    /* 摘除中断回调（如果注册过） */
    if (f->user_cb)
    {
        gpio_pin_interrupt_configure(f->drv->dev, f->pin, GPIO_INT_DISABLE);
        gpio_remove_callback(f->drv->dev, &f->zephyr_cb);
    }
    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read：读引脚电平 ---------- */
static posix_ssize_t gpio_read(void *drv_data, void *file_priv,
                               void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f = (gpio_file_t *)file_priv;

    int ret = gpio_pin_get_raw(f->drv->dev, f->pin);
    if (ret < 0) { return POSIX_ERR_IO; }
    *(int *)buf = ret;
    return (posix_ssize_t)sizeof(int);
}

/* ---------- write：写引脚电平 ---------- */
static posix_ssize_t gpio_write(void *drv_data, void *file_priv,
                                const void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f   = (gpio_file_t *)file_priv;
    int          val = *(const int *)buf;

    int ret = gpio_pin_set_raw(f->drv->dev, f->pin, val ? 1 : 0);
    return (ret < 0) ? POSIX_ERR_IO : (posix_ssize_t)sizeof(int);
}

/* ---------- ioctl ---------- */
static int gpio_ioctl(void *drv_data, void *file_priv,
                      unsigned long cmd, void *arg)
{
    (void)drv_data;
    gpio_file_t *f = (gpio_file_t *)file_priv;

    switch (cmd)
    {
    case POSIX_GPIO_IOCTL_SET_DIR:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_config_t *cfg = (posix_gpio_config_t *)arg;

            gpio_flags_t flags = 0;

            /* 方向 */
            if (cfg->direction == POSIX_GPIO_DIR_OUTPUT)
            {
                flags |= cfg->initial_value ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW;
            }
            else if (cfg->direction == POSIX_GPIO_DIR_OPEN_DRAIN)
            {
                flags |= GPIO_OUTPUT | GPIO_OPEN_DRAIN;
                flags |= cfg->initial_value ? GPIO_OUTPUT_INIT_HIGH : GPIO_OUTPUT_INIT_LOW;
            }
            else
            {
                flags |= GPIO_INPUT;
            }

            /* 上下拉 */
            if (cfg->pull == POSIX_GPIO_PULL_UP)        { flags |= GPIO_PULL_UP; }
            else if (cfg->pull == POSIX_GPIO_PULL_DOWN) { flags |= GPIO_PULL_DOWN; }

            int ret = gpio_pin_configure(f->drv->dev, f->pin, flags);
            if (ret < 0) { return POSIX_ERR_IO; }

            f->direction = cfg->direction;
            f->pull      = cfg->pull;
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_GET_VALUE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            int ret = gpio_pin_get_raw(f->drv->dev, f->pin);
            if (ret < 0) { return POSIX_ERR_IO; }
            v->value = ret;
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_VALUE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            int ret = gpio_pin_set_raw(f->drv->dev, f->pin, v->value ? 1 : 0);
            return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_IRQ:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_irq_t *irq = (posix_gpio_irq_t *)arg;

            /* 触发模式映射 */
            gpio_flags_t int_flags;
            switch (irq->trigger)
            {
            case POSIX_GPIO_INT_RISING:     int_flags = GPIO_INT_EDGE_RISING;  break;
            case POSIX_GPIO_INT_FALLING:    int_flags = GPIO_INT_EDGE_FALLING; break;
            case POSIX_GPIO_INT_BOTH:       int_flags = GPIO_INT_EDGE_BOTH;    break;
            case POSIX_GPIO_INT_LOW_LEVEL:  int_flags = GPIO_INT_LEVEL_LOW;    break;
            case POSIX_GPIO_INT_HIGH_LEVEL: int_flags = GPIO_INT_LEVEL_HIGH;   break;
            case POSIX_GPIO_INT_DISABLE:
                gpio_pin_interrupt_configure(f->drv->dev, f->pin, GPIO_INT_DISABLE);
                gpio_remove_callback(f->drv->dev, &f->zephyr_cb);
                f->user_cb  = NULL;
                f->user_arg = NULL;
                return POSIX_OK;
            default:
                return POSIX_ERR_INVAL;
            }

            /* 摘除旧回调（如果有） */
            if (f->user_cb)
            {
                gpio_remove_callback(f->drv->dev, &f->zephyr_cb);
            }

            f->user_cb  = irq->callback;
            f->user_arg = irq->arg;
            f->int_flags = int_flags;

            gpio_init_callback(&f->zephyr_cb, gpio_zephyr_isr, BIT(f->pin));
            gpio_add_callback(f->drv->dev, &f->zephyr_cb);

            /* 先不使能，等 ENABLE_IRQ 时才打开 */
            gpio_pin_interrupt_configure(f->drv->dev, f->pin, GPIO_INT_DISABLE);
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_ENABLE_IRQ:
        {
            if (!f->user_cb) { return POSIX_ERR_INVAL; }
            int ret = gpio_pin_interrupt_configure(f->drv->dev, f->pin, f->int_flags);
            return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_DISABLE_IRQ:
        {
            int ret = gpio_pin_interrupt_configure(f->drv->dev, f->pin,
                                                   GPIO_INT_MODE_DISABLE_ONLY);
            return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_gpio_ops =
{
    .open  = gpio_open,
    .close = gpio_close,
    .read  = gpio_read,
    .write = gpio_write,
    .ioctl = gpio_ioctl,
};

/* ---------- 设备实例 ---------- */
static gpio_drv_data_t s_gpio0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(gpioa)) };
static gpio_drv_data_t s_gpio1 = { .dev = DEVICE_DT_GET(DT_NODELABEL(gpiob)) };

/* ---------- 自动注册 ---------- */
static int gpio_init(void)
{
    int ret;
    ret = posix_device_register("/dev/gpio0", &g_gpio_ops, &s_gpio0);
    if (ret) { return ret; }
    ret = posix_device_register("/dev/gpio1", &g_gpio_ops, &s_gpio1);
    return ret;
}
POSIX_INIT_DEVICE_EXPORT(gpio_init);
