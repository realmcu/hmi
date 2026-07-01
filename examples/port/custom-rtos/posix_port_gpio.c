/* ================================================================
 * GPIO 驱动 POSIX 端口（RTL87x3G / RTL876x 原生驱动直调）
 *
 * 路径格式: /dev/gpio<N>/p<Pin> 或 /dev/gpio<N>/pin<Pin>
 *   /dev/gpio0/pXX  → GPIOA 组，pin_index = XX (0~31)
 *   /dev/gpio1/pXX  → GPIOB 组，pin_index = XX (0~31)
 *
 * 注意：这里的 pin_index 是"组内引脚号"（GPIOA0~GPIOA31 对应 0~31），
 * 与芯片全局 pin 号（P0_0~P4_x）通过 rtl876x GPIO_GetNum() 映射相关。
 * 应用层用哪个约定？为了跟 Zephyr GPIO subsystem 概念对齐，本文件采用
 * "port + pin_index"（组内 0~31）。若需要用芯片 pin 号 P3_5 打开，
 * 请先用 GPIO_GetNum(P3_5) 换算成 pin_index 再拼路径。
 *
 * 中断分派模型（参考 Zephyr gpio_rtl87x3g.c）：
 *   - 芯片 GPIO 中断按组共享：A0/A1 独立，A2-7 / A8-15 / A16-23 / A24-31
 *     四组共享一个物理 IRQn；GPIOB 同理。
 *   - 一次 SET_IRQ 时用 irq_connect_dynamic() 把对应 IRQn 挂到
 *     gpio_port_isr()。同一 port 32 个 pin 共用一个 dispatcher。
 *   - dispatcher 读 GPIO_TypeDef->INTSTATUS 拿到当次触发 pin 位图，
 *     查 g_port_state[port].cb[pin_index] 分派用户回调，清 pending。
 *
 * 依赖：Zephyr CONFIG_GPIO=y 但 dts 中 gpioa/gpiob 保持 disabled
 *       （避免 Zephyr 官方 driver 抢占 irq_connect_dynamic 的目标）
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "rtl876x.h"
#include "rtl876x_rcc.h"
#include "rtl876x_pinmux.h"
#include "rtl876x_gpio.h"
#include "rtl876x_nvic.h"

#include <zephyr/irq.h>

/* ---------- 每 port 32 pin 的中断状态 ---------- */
typedef struct
{
    void (*callback)(void *arg);
    void  *arg;
    uint8_t abs_pin;                 /* 全局 pin 号（P0_0..P4_x），供 hw 调用 */
    uint8_t reserved;
} gpio_pin_state_t;

typedef struct
{
    GPIO_TypeDef *port_base;         /* GPIOA / GPIOB */
    uint32_t      apb_periph;
    uint32_t      apb_clock;
    uint8_t       inited;
    gpio_pin_state_t pins[32];
    uint32_t      irq_registered_mask; /* bit i = 该 pin 的 IRQn 已 dynamic-connect */
} gpio_port_state_t;

static gpio_port_state_t s_ports[2];    /* [0]=GPIOA, [1]=GPIOB */

/* ---------- GPIO 控制器"驱动私有数据"（每个 posix 设备一份） ---------- */
typedef struct
{
    int                unit;         /* 0=GPIOA, 1=GPIOB */
    gpio_port_state_t *state;
} gpio_drv_data_t;

static gpio_drv_data_t s_gpio0 = { .unit = 0, .state = &s_ports[0] };
static gpio_drv_data_t s_gpio1 = { .unit = 1, .state = &s_ports[1] };

/* ---------- per-open file 私有 ---------- */
typedef struct
{
    bool             in_use;
    gpio_drv_data_t *drv;
    int              pin;            /* 组内 pin_index 0~31 */
    uint8_t          abs_pin;        /* 全局 pin 号（板级 P3_5 = 27 等） */
    uint8_t          direction;
    uint8_t          pull;
} gpio_file_t;

#define MAX_GPIO_FILES   16
static gpio_file_t s_gpio_files[MAX_GPIO_FILES];

/* ================================================================
 * pin_index (0~31) + port_unit (0/1) → 全局 pin 号
 *
 * RTL87x3G 里 GPIO_GetNum(P3_5) = 26（即 GPIOA26，属 GPIOA port, index 26）。
 * 反向映射（组+index → 全局 pin）由芯片手册决定，多数板子上不是简单加法：
 * 需要走 PIN_INDEX 反查。为简化 posix 层 open 语义，这里让应用**显式**通过
 * pin_index 打开——即路径里的编号就是 GPIOA/GPIOB 组内 0~31 的 index，
 * 而不是 P3_5 这种全局号。
 *
 * 但硬件 API（Pinmux_Config / Pad_Config）需要全局 pin 号。因此 SET_DIR
 * 时应用要提供 abs_pin（把它塞进 posix_gpio_config_t.pin 字段——原本这个
 * 字段被文档说"fd 已绑定 pin，填 0"，这里我们语义扩展：**若 cfg->pin>0
 * 视为芯片全局 pin 号，用于 Pinmux/Pad 配置**；否则跳过 Pinmux/Pad 步骤
 * 假设应用已在别处配好 pad。
 * ================================================================ */

/* 目前 posix 层用组内 pin_index，直接从 fd 走 port_base + (1<<pin_idx)。
 * 若将来支持"以芯片全局 pin 号打开"，可以用 SDK 的 GPIO_GetPort()/
 * GPIO_GetPinBit() 反查——留给下次改动时再引入。 */

/* ---------- port 初始化（时钟 + 每 port 一次） ---------- */
static void gpio_port_hw_init(gpio_port_state_t *st, int unit)
{
    if (st->inited) { return; }
    st->port_base  = (unit == 0) ? GPIOA : GPIOB;
    st->apb_periph = (unit == 0) ? APBPeriph_GPIOA : APBPeriph_GPIOB;
    st->apb_clock  = (unit == 0) ? APBPeriph_GPIOA_CLOCK : APBPeriph_GPIOB_CLOCK;
    RCC_PeriphClockCmd(st->apb_periph, st->apb_clock, ENABLE);
    st->inited = 1;
}

/* ---------- 统一中断 dispatcher（port-level） ----------
 * 参考 gpio_rtl87x3g.c 的做法：读 INTSTATUS 一次拿位图，遍历分派。
 * 用 GPIO_GetINTStatus 逐 pin 查（虽然多几次 MMIO 但代码更可移植）。
 */
static void gpio_port_isr(const void *arg)
{
    gpio_port_state_t *st = (gpio_port_state_t *)arg;
    GPIO_TypeDef *bus = st->port_base;

    for (int i = 0; i < 32; i++)
    {
        uint32_t bit = 1u << i;
        if (GPIO_GetINTStatus(bus, bit) != SET) { continue; }

        /* 屏蔽 + 派发 + 清 pending（RTL 官方推荐顺序） */
        GPIO_MaskINTConfig(bus, bit, ENABLE);

        gpio_pin_state_t *ps = &st->pins[i];
        if (ps->callback) { ps->callback(ps->arg); }

        GPIO_ClearINTPendingBit(bus, bit);
        GPIO_MaskINTConfig(bus, bit, DISABLE);
    }
}

/* pin_index → 物理 IRQn（每 group 共享）
 *
 * GPIO 中断按组共享 NVIC 向量（见 vector_table_auto_gen.h）：
 *   GPIOA:  pin0 → 18   pin1 → 19
 *           pin2..7   → 20 (GPIO_A2_7)
 *           pin8..15  → 21 (GPIO_A8_15)
 *           pin16..23 → 22 (GPIO_A16_23)
 *           pin24..31 → 23 (GPIO_A24_31)
 *   GPIOB:  pin0..7   → 37 (GPIO_B0_7)
 *           pin8..15  → 38
 *           pin16..23 → 39
 *           pin24..31 → 40
 *
 * 同组多 pin 共用一个物理 IRQn，都指向本文件的 gpio_port_isr，
 * dispatcher 内部再用 GPIO_GetINTStatus 遍历分派到具体 pin。
 */
static unsigned gpio_pin_to_irqn(int port_unit, int pin_idx)
{
    if (port_unit == 0)
    {
        if (pin_idx == 0)              { return 18; }  /* GPIO_A0_IRQn  */
        if (pin_idx == 1)              { return 19; }  /* GPIO_A1_IRQn  */
        if (pin_idx <= 7)              { return 20; }  /* GPIO_A2_7     */
        if (pin_idx <= 15)             { return 21; }  /* GPIO_A8_15    */
        if (pin_idx <= 23)             { return 22; }  /* GPIO_A16_23   */
        return 23;                                     /* GPIO_A24_31   */
    }
    if (pin_idx <= 7)                  { return 37; }  /* GPIO_B0_7     */
    if (pin_idx <= 15)                 { return 38; }  /* GPIO_B8_15    */
    if (pin_idx <= 23)                 { return 39; }  /* GPIO_B16_23   */
    return 40;                                         /* GPIO_B24_31   */
}

/* ---------- open：解析路径 pin_index ---------- */
static void *gpio_open(void *drv_data, const char *path)
{
    gpio_drv_data_t *d = (gpio_drv_data_t *)drv_data;

    const char *p = strstr(path, "/p");
    if (!p) { return POSIX_OPEN_ERR; }
    p++;                           /* 跳过 '/' */
    if (*p == 'p') { p++; }        /* 跳过 'p' */
    if (*p == 'i') { p += 2; }     /* 跳过 "in" (pin 风格) */

    const char *digit_start = p;
    char *end;
    long pin = strtol(p, &end, 10);
    if (end == digit_start || *end != '\0' || pin < 0 || pin > 31)
    {
        return POSIX_OPEN_ERR;
    }

    gpio_file_t *f = NULL;
    for (int i = 0; i < MAX_GPIO_FILES; i++)
    {
        if (!s_gpio_files[i].in_use)
        {
            s_gpio_files[i].in_use = true;
            f = &s_gpio_files[i];
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    gpio_port_hw_init(d->state, d->unit);

    f->drv       = d;
    f->pin       = (int)pin;
    f->abs_pin   = 0;              /* SET_DIR 时应用可通过 cfg->pin 提供 */
    f->direction = POSIX_GPIO_DIR_INPUT;
    f->pull      = POSIX_GPIO_PULL_NONE;
    return f;
}

static int gpio_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    gpio_file_t *f = (gpio_file_t *)file_priv;
    if (f && f->in_use)
    {
        /* 卸中断（如果注册过） */
        uint32_t bit = 1u << f->pin;
        GPIO_INTConfig(f->drv->state->port_base, bit, DISABLE);
        f->drv->state->pins[f->pin].callback = NULL;
        f->drv->state->pins[f->pin].arg      = NULL;
        f->in_use = false;
    }
    return POSIX_OK;
}

/* ---------- 应用 cfg->pin → abs_pin + pinmux + pad 配置 ---------- */
static void gpio_apply_pinmux(gpio_file_t *f, uint8_t abs_pin,
                              uint8_t direction, uint8_t pull)
{
    uint8_t pad_pull = (pull == POSIX_GPIO_PULL_UP)   ? PAD_PULL_UP :
                       (pull == POSIX_GPIO_PULL_DOWN) ? PAD_PULL_DOWN :
                       PAD_PULL_NONE;
    Pad_PullConfigValue(abs_pin, 1);
    Pinmux_Config(abs_pin, DWGPIO);
    if (direction == POSIX_GPIO_DIR_OUTPUT)
    {
        Pad_Config(abs_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, pad_pull,
                   PAD_OUT_ENABLE, PAD_OUT_LOW);
    }
    else
    {
        Pad_Config(abs_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, pad_pull,
                   PAD_OUT_DISABLE, PAD_OUT_LOW);
    }
    f->abs_pin = abs_pin;
}

/* ---------- read/write：走 hw 读写 ---------- */
static posix_ssize_t gpio_read(void *drv_data, void *file_priv,
                               void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f = (gpio_file_t *)file_priv;
    uint32_t bit = 1u << f->pin;
    *(int *)buf = GPIO_ReadInputDataBit(f->drv->state->port_base, bit) ? 1 : 0;
    return (posix_ssize_t)sizeof(int);
}

static posix_ssize_t gpio_write(void *drv_data, void *file_priv,
                                const void *buf, size_t count)
{
    (void)drv_data;
    if (count < sizeof(int)) { return POSIX_ERR_INVAL; }
    gpio_file_t *f   = (gpio_file_t *)file_priv;
    int          val = *(const int *)buf;
    uint32_t bit = 1u << f->pin;
    GPIO_WriteBit(f->drv->state->port_base, bit, val ? Bit_SET : Bit_RESET);
    return (posix_ssize_t)sizeof(int);
}

/* ---------- ioctl ---------- */
static int gpio_ioctl(void *drv_data, void *file_priv,
                      unsigned long cmd, void *arg)
{
    gpio_file_t     *f  = (gpio_file_t *)file_priv;
    gpio_drv_data_t *d  = (gpio_drv_data_t *)drv_data;
    gpio_port_state_t *st = d->state;
    GPIO_TypeDef *bus = st->port_base;
    uint32_t pin_bit = 1u << f->pin;

    switch (cmd)
    {
    case POSIX_GPIO_IOCTL_SET_DIR:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_config_t *cfg = (posix_gpio_config_t *)arg;

            /* 若应用给了 cfg->pin（全局 pin 号），做完整 pinmux+pad 配置 */
            if (cfg->pin > 0)
            {
                gpio_apply_pinmux(f, (uint8_t)cfg->pin, cfg->direction, cfg->pull);
            }
            f->direction = cfg->direction;
            f->pull      = cfg->pull;

            /* 用 GPIO_SetDirection 设方向（不清中断配置） */
            GPIO_SetDirection(bus, pin_bit,
                              (cfg->direction == POSIX_GPIO_DIR_OUTPUT)
                              ? GPIO_Mode_OUT : GPIO_Mode_IN);
            if (cfg->direction == POSIX_GPIO_DIR_OUTPUT)
            {
                GPIO_WriteBit(bus, pin_bit,
                              cfg->initial_value ? Bit_SET : Bit_RESET);
            }
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_GET_VALUE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            v->value = GPIO_ReadInputDataBit(bus, pin_bit) ? 1 : 0;
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_VALUE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_value_t *v = (posix_gpio_value_t *)arg;
            GPIO_WriteBit(bus, pin_bit, v->value ? Bit_SET : Bit_RESET);
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_SET_IRQ:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_gpio_irq_t *irq = (posix_gpio_irq_t *)arg;

            /* 存回调 */
            st->pins[f->pin].callback = irq->callback;
            st->pins[f->pin].arg      = irq->arg;

            /* 写触发极性 / 边沿 */
            GPIO_InitTypeDef init;
            GPIO_StructInit(&init);
            init.GPIO_PinBit    = pin_bit;
            init.GPIO_Mode      = GPIO_Mode_IN;
            init.GPIO_ITCmd     = ENABLE;
            init.GPIO_ITDebounce = GPIO_INT_DEBOUNCE_DISABLE;

            switch (irq->trigger)
            {
            case POSIX_GPIO_INT_RISING:
                init.GPIO_ITTrigger  = GPIO_INT_TRIGGER_EDGE;
                init.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_HIGH;
                break;
            case POSIX_GPIO_INT_FALLING:
                init.GPIO_ITTrigger  = GPIO_INT_TRIGGER_EDGE;
                init.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_LOW;
                break;
            case POSIX_GPIO_INT_BOTH:
#if GPIO_SUPPORT_INT_BOTHEDGE
                init.GPIO_ITTrigger  = GPIO_INT_TRIGGER_BOTH_EDGE;
                init.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_HIGH;
#else
                return POSIX_ERR_NOSUPP;
#endif
                break;
            case POSIX_GPIO_INT_LOW_LEVEL:
                init.GPIO_ITTrigger  = GPIO_INT_TRIGGER_LEVEL;
                init.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_LOW;
                break;
            case POSIX_GPIO_INT_HIGH_LEVEL:
                init.GPIO_ITTrigger  = GPIO_INT_TRIGGER_LEVEL;
                init.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_HIGH;
                break;
            default:
                return POSIX_ERR_INVAL;
            }
            GPIOx_Init(bus, &init);

            /* 挂 IRQn → gpio_port_isr（同 port 内多次调用会覆盖成同一 arg，OK） */
            unsigned irqn = gpio_pin_to_irqn(d->unit, f->pin);
            irq_connect_dynamic(irqn, 3, gpio_port_isr, st, 0);
            st->irq_registered_mask |= pin_bit;

            /* 先屏蔽，等 ENABLE_IRQ 打开 */
            GPIO_MaskINTConfig(bus, pin_bit, ENABLE);
            GPIO_INTConfig(bus, pin_bit, ENABLE);
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_ENABLE_IRQ:
        {
            unsigned irqn = gpio_pin_to_irqn(d->unit, f->pin);
            GPIO_ClearINTPendingBit(bus, pin_bit);
            GPIO_MaskINTConfig(bus, pin_bit, DISABLE);
            irq_enable(irqn);
            return POSIX_OK;
        }

    case POSIX_GPIO_IOCTL_DISABLE_IRQ:
        {
            GPIO_MaskINTConfig(bus, pin_bit, ENABLE);
            GPIO_INTConfig(bus, pin_bit, DISABLE);
            /* 不动 NVIC——同 IRQn 可能还挂着其它 pin */
            return POSIX_OK;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 + 自动注册 ---------- */
const posix_driver_ops_t g_gpio_ops =
{
    .open  = gpio_open,
    .close = gpio_close,
    .read  = gpio_read,
    .write = gpio_write,
    .ioctl = gpio_ioctl,
};

static int gpio_init(void)
{
    int ret;
    ret = posix_device_register("/dev/gpio0", &g_gpio_ops, &s_gpio0);
    if (ret) { return ret; }
    ret = posix_device_register("/dev/gpio1", &g_gpio_ops, &s_gpio1);
    return ret;
}
POSIX_INIT_DEVICE_EXPORT(gpio_init);
