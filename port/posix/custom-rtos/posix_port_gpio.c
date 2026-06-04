/* ================================================================
 * GPIO 驱动示例 — 引脚级 fd 风格
 *
 * 路径格式: /dev/gpio<N>/p<Pin>
 *   例如:   /dev/gpio0/p12  → GPIO0 的 pin 12
 *           /dev/gpio1/p0   → GPIO1 的 pin 0
 *
 * 每个 open 返回一个 fd，该 fd 绑定到特定引脚。
 * read  = 读引脚电平
 * write = 写引脚电平
 * ioctl = 配置方向/上拉/中断
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <string.h>
#include <stdlib.h>

/* ---------- GPIO 控制器私有数据 ---------- */
/* ---------- 静态文件池 ---------- */
#define MAX_GPIO_FILES   16
static gpio_file_t s_gpio_files[MAX_GPIO_FILES];
static int s_gpio_file_used[MAX_GPIO_FILES];

typedef struct
{
    int       unit;
    uintptr_t reg_base;          /* 寄存器基址 */
} gpio_drv_data_t;

/* ---------- per-open 私有数据（每个 fd 一个） ---------- */
typedef struct
{
    gpio_drv_data_t *drv;        /* 指向控制器 */
    int             pin;         /* 绑定的引脚号 */
    uint8_t         direction;   /* 缓存 */
    uint8_t         pull;
} gpio_file_t;

/* ---------- open：解析路径，绑定 pin ---------- */
static void *gpio_open(void *drv_data, const char *path)
{
    gpio_drv_data_t *d = (gpio_drv_data_t *)drv_data;

    /* 解析 pin 号：找 "/p" 后的数字 */
    const char *p = strstr(path, "/p");
    if (!p) { return POSIX_OPEN_ERR; }
    p++;                    /* 跳过 '/' */
    if (*p == 'p') { p++; }
    if (*p == 'i') { p += 2; }  /* 跳过 "in" */

    char *end;
    long pin = strtol(p, &end, 10);
    if (*end != '\0' || pin < 0 || pin > 255) { return POSIX_OPEN_ERR; }

    /* 从静态池分配 */
    gpio_file_t *f = NULL;
    for (int i = 0; i < MAX_GPIO_FILES; i++)
    {
        if (!s_gpio_file_used[i])
        {
            s_gpio_file_used[i] = 1;
            f = &s_gpio_files[i];
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv       = d;
    f->pin       = (int)pin;
    f->direction = POSIX_GPIO_DIR_INPUT;
    f->pull      = POSIX_GPIO_PULL_NONE;
    return f;
}

static int gpio_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    /* 归还到静态池 */
    gpio_file_t *f = (gpio_file_t *)file_priv;
    int idx = f - s_gpio_files;
    if (idx >= 0 && idx < MAX_GPIO_FILES)
    {
        s_gpio_file_used[idx] = 0;
    }
    return POSIX_OK;
}

/* ---------- read：读引脚电平 ---------- */
static posix_ssize_t gpio_read(void *drv_data, void *file_priv,
                               void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f = (gpio_file_t *)file_priv;
    /* *(int*)buf = hw_gpio_read(f->drv->reg_base, f->pin); */
    return (posix_ssize_t)sizeof(int);
}

/* ---------- write：写引脚电平 ---------- */
static posix_ssize_t gpio_write(void *drv_data, void *file_priv,
                                const void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f = (gpio_file_t *)file_priv;
    int val = *(const int *)buf;
    /* hw_gpio_write(f->drv->reg_base, f->pin, val); */
    (void)val;
    return (posix_ssize_t)sizeof(int);
}

/* ---------- ioctl ---------- */
static int gpio_ioctl(void *drv_data, void *file_priv,
                      unsigned long cmd, void *arg)
{
    gpio_file_t *f = (gpio_file_t *)file_priv;
    gpio_drv_data_t *d = (gpio_drv_data_t *)drv_data;

    switch (cmd)
    {
    case POSIX_GPIO_IOCTL_SET_DIR:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            posix_gpio_config_t *cfg = (posix_gpio_config_t *)arg;
            f->direction = cfg->direction;
            f->pull      = cfg->pull;
            /* hw_gpio_set_mode(d->reg_base, f->pin, cfg->direction, cfg->pull); */
            if (cfg->direction == POSIX_GPIO_DIR_OUTPUT)
            {
                /* hw_gpio_write(d->reg_base, f->pin, cfg->initial_value); */
            }
            return POSIX_OK;
        }
    case POSIX_GPIO_IOCTL_GET_VALUE:
        {
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            /* v->value = hw_gpio_read(d->reg_base, f->pin); */
            return POSIX_OK;
        }
    case POSIX_GPIO_IOCTL_SET_VALUE:
        {
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            /* hw_gpio_write(d->reg_base, f->pin, v->value); */
            (void)v;
            return POSIX_OK;
        }
    case POSIX_GPIO_IOCTL_SET_IRQ:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            posix_gpio_irq_t *irq = (posix_gpio_irq_t *)arg;
            /* hw_gpio_set_irq(d->reg_base, f->pin,
             *     irq->trigger, irq->callback, irq->arg); */
            (void)irq;
            return POSIX_OK;
        }
    case POSIX_GPIO_IOCTL_ENABLE_IRQ:
        /* hw_gpio_irq_enable(d->reg_base, f->pin, 1); */
        return POSIX_OK;
    case POSIX_GPIO_IOCTL_DISABLE_IRQ:
        /* hw_gpio_irq_enable(d->reg_base, f->pin, 0); */
        return POSIX_OK;
    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
const posix_driver_ops_t g_gpio_ops =
{
    .open  = gpio_open,
    .close = gpio_close,
    .read  = gpio_read,
    .write = gpio_write,
    .ioctl = gpio_ioctl,
};

/* ---------- 设备实例 + 自动注册 ---------- */
static gpio_drv_data_t s_gpio0 = { .unit = 0, .reg_base = 0x40010000 };
static gpio_drv_data_t s_gpio1 = { .unit = 1, .reg_base = 0x40011000 };

static int gpio_init(void)
{
    int ret;
    ret = posix_device_register("/dev/gpio0", &g_gpio_ops, &s_gpio0);
    if (ret) { return ret; }
    ret = posix_device_register("/dev/gpio1", &g_gpio_ops, &s_gpio1);
    return ret;
}
POSIX_INIT_DEVICE_EXPORT(gpio_init);
