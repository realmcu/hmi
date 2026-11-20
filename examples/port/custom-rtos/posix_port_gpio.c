/* ================================================================
 * GPIO driver POSIX port (RTL87x3G / RTL876x native driver direct calls)
 *
 * Path format: /dev/gpio<N>/p<Pin> or /dev/gpio<N>/pin<Pin>
 *   /dev/gpio0/pXX  → GPIOA group, pin_index = XX (0~31)
 *   /dev/gpio1/pXX  → GPIOB group, pin_index = XX (0~31)
 *
 * Note: pin_index here is the "in-group pin number" (GPIOA0~GPIOA31 map to 0~31),
 * related to the chip global pin number (P0_0~P4_x) via the rtl876x GPIO_GetNum() mapping.
 * Which convention does the app layer use? To align with the Zephyr GPIO subsystem concept, this file uses
 * "port + pin_index" (in-group 0~31). To open by chip pin number such as P3_5,
 * first convert with GPIO_GetNum(P3_5) to pin_index, then build the path.
 *
 * Interrupt dispatch model (see Zephyr gpio_rtl87x3g.c):
 *   - Chip GPIO interrupts are shared per group: A0/A1 are separate, A2-7 / A8-15 / A16-23 / A24-31
 *     share one physical IRQn; same for GPIOB.
 *   - On SET_IRQ, irq_connect_dynamic() attaches the corresponding IRQn to
 *     gpio_port_isr(). All 32 pins of the same port share one dispatcher.
 *   - The dispatcher reads GPIO_TypeDef->INTSTATUS to get the triggered pin bitmap for this event,
 *     looks up g_port_state[port].cb[pin_index] to dispatch to user callbacks, and clears pending.
 *
 * Dependency: Zephyr CONFIG_GPIO=y but gpioa/gpiob stay disabled in the dts
 *       (so the official Zephyr driver does not claim the irq_connect_dynamic target)
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

/* ---------- Interrupt state for 32 pins per port ---------- */
typedef struct
{
    void (*callback)(void *arg);
    void  *arg;
    uint8_t abs_pin;                 /* global pin number (P0_0..P4_x), used for hw calls */
    uint8_t reserved;
} gpio_pin_state_t;

typedef struct
{
    GPIO_TypeDef *port_base;         /* GPIOA / GPIOB */
    uint32_t      apb_periph;
    uint32_t      apb_clock;
    uint8_t       inited;
    gpio_pin_state_t pins[32];
    uint32_t      irq_registered_mask; /* bit i = that pin's IRQn is dynamic-connected */
} gpio_port_state_t;

static gpio_port_state_t s_ports[2];    /* [0]=GPIOA, [1]=GPIOB */

/* ---------- GPIO controller "driver private data" (one per posix device) ---------- */
typedef struct
{
    int                unit;         /* 0=GPIOA, 1=GPIOB */
    gpio_port_state_t *state;
} gpio_drv_data_t;

static gpio_drv_data_t s_gpio0 = { .unit = 0, .state = &s_ports[0] };
static gpio_drv_data_t s_gpio1 = { .unit = 1, .state = &s_ports[1] };

/* ---------- per-open file private data ---------- */
typedef struct
{
    bool             in_use;
    gpio_drv_data_t *drv;
    int              pin;            /* in-group pin_index 0~31 */
    uint8_t          abs_pin;        /* global pin number (board-level P3_5 = 27 etc.) */
    uint8_t          direction;
    uint8_t          pull;
} gpio_file_t;

#define MAX_GPIO_FILES   16
static gpio_file_t s_gpio_files[MAX_GPIO_FILES];

/* ================================================================
 * pin_index (0~31) + port_unit (0/1) → global pin number
 *
 * In RTL87x3G, GPIO_GetNum(P3_5) = 26 (i.e. GPIOA26, in the GPIOA port, index 26).
 * The reverse mapping (group+index → global pin) is defined by the chip manual and is not simple addition on most boards:
 * it requires a PIN_INDEX reverse lookup. To simplify posix-layer open semantics, the app opens **explicitly** by
 * pin_index — i.e. the number in the path is the in-group 0~31 index of GPIOA/GPIOB,
 * not a global number like P3_5.
 *
 * But the hardware API (Pinmux_Config / Pad_Config) needs the global pin number. So on SET_DIR
 * the app must provide abs_pin (placed in the posix_gpio_config_t.pin field — this field was
 * originally documented as "fd already bound to pin, put 0"; here we extend the semantics: **if cfg->pin>0
 * it is treated as the chip global pin number, used for Pinmux/Pad config**; otherwise the Pinmux/Pad step is
 * skipped, assuming the app has already configured the pad elsewhere.
 * ================================================================ */

/* The posix layer currently uses in-group pin_index, going directly from the fd to port_base + (1<<pin_idx).
 * If "open by chip global pin number" is supported in the future, the SDK's GPIO_GetPort()/
 * GPIO_GetPinBit() reverse lookup can be used — to be introduced in a later change. */

/* ---------- port init (clock, once per port) ---------- */
static void gpio_port_hw_init(gpio_port_state_t *st, int unit)
{
    if (st->inited) { return; }
    st->port_base  = (unit == 0) ? GPIOA : GPIOB;
    st->apb_periph = (unit == 0) ? APBPeriph_GPIOA : APBPeriph_GPIOB;
    st->apb_clock  = (unit == 0) ? APBPeriph_GPIOA_CLOCK : APBPeriph_GPIOB_CLOCK;
    RCC_PeriphClockCmd(st->apb_periph, st->apb_clock, ENABLE);
    st->inited = 1;
}

/* ---------- Unified interrupt dispatcher (port-level) ----------
 * Following gpio_rtl87x3g.c: read INTSTATUS once to get the bitmap, then iterate and dispatch.
 * Uses GPIO_GetINTStatus per pin (a few extra MMIO reads but more portable code).
 */
static void gpio_port_isr(const void *arg)
{
    gpio_port_state_t *st = (gpio_port_state_t *)arg;
    GPIO_TypeDef *bus = st->port_base;

    for (int i = 0; i < 32; i++)
    {
        uint32_t bit = 1u << i;
        if (GPIO_GetINTStatus(bus, bit) != SET) { continue; }

        /* Mask + dispatch + clear pending (RTL-recommended order) */
        GPIO_MaskINTConfig(bus, bit, ENABLE);

        gpio_pin_state_t *ps = &st->pins[i];
        if (ps->callback) { ps->callback(ps->arg); }

        GPIO_ClearINTPendingBit(bus, bit);
        GPIO_MaskINTConfig(bus, bit, DISABLE);
    }
}

/* pin_index → physical IRQn (shared per group)
 *
 * GPIO interrupts share NVIC vectors per group (see vector_table_auto_gen.h):
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
 * Multiple pins in the same group share one physical IRQn, all pointing to this file's gpio_port_isr;
 * the dispatcher then uses GPIO_GetINTStatus to iterate and dispatch to the specific pin.
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

/* ---------- open: parse pin_index from the path ---------- */
static void *gpio_open(void *drv_data, const char *path)
{
    gpio_drv_data_t *d = (gpio_drv_data_t *)drv_data;

    const char *p = strstr(path, "/p");
    if (!p) { return POSIX_OPEN_ERR; }
    p++;                           /* skip '/' */
    if (*p == 'p') { p++; }        /* skip 'p' */
    if (*p == 'i') { p += 2; }     /* skip "in" (pin style) */

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
    f->abs_pin   = 0;              /* the app can provide it via cfg->pin on SET_DIR */
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
        /* Remove the interrupt (if registered) */
        uint32_t bit = 1u << f->pin;
        GPIO_INTConfig(f->drv->state->port_base, bit, DISABLE);
        f->drv->state->pins[f->pin].callback = NULL;
        f->drv->state->pins[f->pin].arg      = NULL;
        f->in_use = false;
    }
    return POSIX_OK;
}

/* ---------- Apply cfg->pin → abs_pin + pinmux + pad config ---------- */
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

/* ---------- read/write: go through hw read/write ---------- */
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

            /* If the app provided cfg->pin (global pin number), do full pinmux+pad config */
            if (cfg->pin > 0)
            {
                gpio_apply_pinmux(f, (uint8_t)cfg->pin, cfg->direction, cfg->pull);
            }
            f->direction = cfg->direction;
            f->pull      = cfg->pull;

            /* Set direction with GPIO_SetDirection (does not clear interrupt config) */
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

            /* Store the callback */
            st->pins[f->pin].callback = irq->callback;
            st->pins[f->pin].arg      = irq->arg;

            /* Set trigger polarity / edge */
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

            /* Attach IRQn → gpio_port_isr (repeated calls within a port overwrite with the same arg, OK) */
            unsigned irqn = gpio_pin_to_irqn(d->unit, f->pin);
            irq_connect_dynamic(irqn, 3, gpio_port_isr, st, 0);
            st->irq_registered_mask |= pin_bit;

            /* Mask first, wait for ENABLE_IRQ to open */
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
            /* Leave NVIC alone — the same IRQn may still have other pins attached */
            return POSIX_OK;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- Driver ops table + auto-registration ---------- */
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
