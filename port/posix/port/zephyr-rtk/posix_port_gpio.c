/* ================================================================
 * GPIO 驱动 — 引脚级 fd 风格 (RTK8773G RTK SDK)
 *
 * 路径格式: /dev/gpio<N>/p<Pin>
 *   例如:   /dev/gpio0/p12  → GPIOA pin 12
 *           /dev/gpio1/p0   → GPIOB pin 0
 *
 * 每个 open 返回一个 fd，该 fd 绑定到特定引脚。
 * read  = 读引脚电平
 * write = 写引脚电平
 * ioctl = 配置方向/上拉/中断
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gpio.h"
#include "rtl876x_gpio.h"
#include <string.h>
#include <stdlib.h>

/* ---------- GPIO 控制器私有数据 ---------- */
typedef struct
{
    int           unit;
    GPIO_TypeDef *port;   /* GPIOA / GPIOB */
} gpio_drv_data_t;

/* ---------- per-open 私有数据（每个 fd 一个） ---------- */
typedef struct
{
    gpio_drv_data_t *drv;        /* 指向控制器 */
    int              pin;         /* 绑定的引脚号 */
    uint8_t          direction;   /* 缓存方向 */
    uint8_t          pull;        /* 缓存上拉 */
} gpio_file_t;

/* ---------- 静态文件池 ---------- */
#define MAX_GPIO_FILES   16
static gpio_file_t s_gpio_files[MAX_GPIO_FILES];
static int         s_gpio_file_used[MAX_GPIO_FILES];

/* ---------- open：解析路径，绑定 pin ---------- */
static void *gpio_open(void *drv_data, const char *path)
{
    gpio_drv_data_t *d = (gpio_drv_data_t *)drv_data;

    /* 解析 pin 号：找 "/p" 后的数字 */
    const char *p = strstr(path, "/p");
    if (!p) { return NULL; }
    p++;                     /* 跳过 '/' */
    if (*p == 'p') { p++; }

    char *end;
    long pin = strtol(p, &end, 10);
    if (*end != '\0' || pin < 0 || pin > 255) { return NULL; }

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
    if (!f) { return NULL; }

    f->drv       = d;
    f->pin       = (int)pin;
    f->direction = POSIX_GPIO_DIR_INPUT;
    f->pull      = POSIX_GPIO_PULL_NONE;
    return f;
}

/* ---------- close：归还到静态池 ---------- */
static int gpio_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    gpio_file_t *f = (gpio_file_t *)file_priv;
    int idx = (int)(f - s_gpio_files);
    if (idx >= 0 && idx < MAX_GPIO_FILES)
    {
        s_gpio_file_used[idx] = 0;
    }
    return POSIX_OK;
}

/* ---------- read：读引脚电平 ---------- */
static int gpio_read(void *drv_data, void *file_priv,
                     void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f = (gpio_file_t *)file_priv;

    uint8_t level = GPIO_ReadInputDataBit(f->drv->port, (uint32_t)(1u << f->pin));
    *(int *)buf = (int)level;
    return (int)sizeof(int);
}

/* ---------- write：写引脚电平 ---------- */
static int gpio_write(void *drv_data, void *file_priv,
                      const void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f = (gpio_file_t *)file_priv;
    int val = *(const int *)buf;

    GPIO_WriteBit(f->drv->port,
                  (uint32_t)(1u << f->pin),
                  val ? Bit_SET : Bit_RESET);
    return (int)sizeof(int);
}

/* ---------- ioctl ---------- */
static int gpio_ioctl(void *drv_data, void *file_priv,
                      unsigned long cmd, void *arg)
{
    gpio_file_t     *f = (gpio_file_t *)file_priv;
    gpio_drv_data_t *d = (gpio_drv_data_t *)drv_data;

    int           is_isr   = (cmd & POSIX_FLAG_ISR) ? 1 : 0;
    unsigned long real_cmd = cmd & ~POSIX_FLAG_ISR;

    switch (real_cmd)
    {

    case POSIX_GPIO_IOCTL_SET_DIR:
        {
            if (is_isr) { return POSIX_ERR_ISR; }
            posix_gpio_config_t *cfg = (posix_gpio_config_t *)arg;
            f->direction = cfg->direction;
            f->pull      = cfg->pull;

            GPIO_InitTypeDef init;
            init.GPIO_PinBit  = (uint32_t)(1u << f->pin);
            init.GPIO_Mode = (cfg->direction == POSIX_GPIO_DIR_OUTPUT)
                             ? GPIO_Mode_OUT
                             : GPIO_Mode_IN;
            GPIOx_Init(d->port, &init);

            if (cfg->direction == POSIX_GPIO_DIR_OUTPUT)
            {
                GPIO_WriteBit(d->port,
                              (uint32_t)(1u << f->pin),
                              cfg->initial_value ? Bit_SET : Bit_RESET);
            }
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_PULL:
        {
            if (is_isr) { return POSIX_ERR_ISR; }
            posix_gpio_config_t *cfg = (posix_gpio_config_t *)arg;
            f->pull = cfg->pull;
            /* RTK SDK does not expose a standalone pull API;
             * pull is configured together with GPIO_Init.
             * Re-init preserving current direction. */
            GPIO_InitTypeDef init;
            init.GPIO_PinBit  = (uint32_t)(1u << f->pin);
            init.GPIO_Mode = (f->direction == POSIX_GPIO_DIR_OUTPUT)
                             ? GPIO_Mode_OUT
                             : GPIO_Mode_IN;
            GPIOx_Init(d->port, &init);
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_GET_VALUE:
        {
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            v->value = (int)GPIO_ReadInputDataBit(f->drv->port,
                                                  (uint32_t)(1u << f->pin));
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_VALUE:
        {
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            GPIO_WriteBit(d->port,
                          (uint32_t)(1u << f->pin),
                          v->value ? Bit_SET : Bit_RESET);
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_MULTI:
        {
            posix_gpio_multi_t *m = (posix_gpio_multi_t *)arg;
            /* Iterate over all set bits in pin_mask */
            uint32_t mask = m->pin_mask;
            while (mask)
            {
                int bit = __builtin_ctz(mask);
                BitAction val = ((m->values >> bit) & 1u) ? Bit_SET : Bit_RESET;
                GPIO_WriteBit(d->port, (uint32_t)(1u << bit), val);
                mask &= mask - 1u;
            }
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_GET_MULTI:
        {
            posix_gpio_multi_t *m = (posix_gpio_multi_t *)arg;
            m->values = 0;
            uint32_t mask = m->pin_mask;
            while (mask)
            {
                int bit = __builtin_ctz(mask);
                uint8_t level = GPIO_ReadInputDataBit(d->port,
                                                      (uint32_t)(1u << bit));
                if (level)
                {
                    m->values |= (uint32_t)(1u << bit);
                }
                mask &= mask - 1u;
            }
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_IRQ:
        {
            if (is_isr) { return POSIX_ERR_ISR; }
            /* posix_gpio_irq_t *irq = (posix_gpio_irq_t *)arg;
             * RTK interrupt registration is board-specific; hook up via
             * platform interrupt manager (not exposed in rtl_gpio.h). */
            (void)arg;
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_ENABLE_IRQ:
        /* Platform-specific interrupt enable — not exposed in rtl_gpio.h */
        return POSIX_OK;

    case POSIX_GPIO_IOCTL_DISABLE_IRQ:
        /* Platform-specific interrupt disable — not exposed in rtl_gpio.h */
        return POSIX_OK;

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
static gpio_drv_data_t s_gpio0 = { .unit = 0, .port = GPIOA };
static gpio_drv_data_t s_gpio1 = { .unit = 1, .port = GPIOB };

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
