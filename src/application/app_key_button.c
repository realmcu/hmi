/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*
 * File   : app_key_button.c
 * Brief  : Board-level physical button driver. Configures the ADC_2 / P3_5
 *          pins as digital input + level interrupt + software debounce,
 *          and updates the state/timestamp global variables subscribed by
 *          the GUI keyboard input device (gui_kb_create) on press/release:
 *              ADC_2 (GPIOA2, "Power" button) -> Menu  (long press -> Power)
 *              P3_5  (GPIOB6, "Home" button)  -> Home  (long press -> Back)
 *          Timestamps all use os_sys_time_get() (ms), the same source as
 *          gui_ms_get(), to keep the long/short press duration judgement
 *          in kb_algo_process() on a consistent unit.
 *
 *          The board only has 2 physical keys but the GUI has 4 logical
 *          keys (Home/Back/Menu/Power). To cover all 4 without touching
 *          any designer-generated GUI code, a press held >= APP_KEY_LONG_
 *          PRESS_MS is forwarded as a synthetic short click of the other
 *          logical key (see app_key_debounce()). Menu is fired on the
 *          short press since it is used far more often than Power, and
 *          Power is relegated to the long press since it is a single,
 *          infrequent action (wake / return to the main dashboard).
 */
#include "board.h"
#include "rtl876x.h"
#include "rtl876x_rcc.h"
#include "rtl876x_gpio.h"
#include "rtl876x_pinmux.h"
#include "rtl876x_nvic.h"
#include "vector_table.h"
#include "os_timer.h"
#include "os_sched.h"
#include "trace.h"
#include "app_key_button.h"

// #define APP_KEY_BUTTON_DEBUG

#ifdef APP_KEY_BUTTON_DEBUG
#define APP_KEY_LOG(...)   DBG_DIRECT(__VA_ARGS__)
#else
#define APP_KEY_LOG(...)
#endif

/* Software debounce duration (ms), matches Wearable key_button_8773e.c */
#define APP_KEY_DEBOUNCE_TIME_MS    20

/* Hold time (ms) above which a press is forwarded to the remapped logical
 * key instead of firing its own physical key, letting 2 physical keys
 * drive all 4 GUI logical keys (Home/Back/Menu/Power). Chosen well below
 * kb_algo_process()'s own 2000ms short/long threshold so the synthesized
 * click is always seen as a short press of the remapped key. */
#define APP_KEY_LONG_PRESS_MS       500

/* Keyboard state/timestamp variables defined on the GUI side (gui_port_indev.c), updated by this driver */
extern bool     power_state;
extern uint32_t power_timestamp_ms_press;
extern uint32_t power_timestamp_ms_release;
extern bool     home_state;
extern uint32_t home_timestamp_ms_press;
extern uint32_t home_timestamp_ms_release;
extern bool     back_state;
extern uint32_t back_timestamp_ms_press;
extern uint32_t back_timestamp_ms_release;
extern bool     menu_state;
extern uint32_t menu_timestamp_ms_press;
extern uint32_t menu_timestamp_ms_release;

typedef struct
{
    uint8_t    pin;             /* Pin (ADC_2 / P3_5) */
    void      *debounce_timer;  /* Software debounce timer handle */
    bool       raw_level;       /* Level read at the moment interrupt is entered */

    bool      *out_state;       /* -> xxx_state */
    uint32_t  *out_press;       /* -> xxx_timestamp_ms_press */
    uint32_t  *out_release;     /* -> xxx_timestamp_ms_release */

    /* Long-press target: only 2 physical keys exist, so a long press is
     * forwarded here as a synthetic short click of the missing logical
     * key (Home->Back, Menu->Power) instead of updating out_*. NULL if
     * this key has no remap target. */
    bool      *remap_state;
    uint32_t  *remap_press;
    uint32_t  *remap_release;
} app_key_button_t;

enum
{
    APP_KEY_POWER = 0,          /* ADC_2 / GPIOA2 */
    APP_KEY_HOME,               /* P3_5  / GPIOB6 */
    APP_KEY_NUM,
};

static app_key_button_t s_keys[APP_KEY_NUM];

/* Determine the group by GPIO logical number (<=GPIO31 is GPIOA, otherwise GPIOB) */
static GPIO_TypeDef *app_key_group(uint8_t pin)
{
    return (GPIO_GetNum(pin) <= GPIO31) ? GPIOA : GPIOB;
}

/* Disable and clear the GPIO interrupt for a key */
static void app_key_int_disable(GPIO_TypeDef *grp, uint32_t pin_bit)
{
    GPIOx_INTConfig(grp, pin_bit, DISABLE);
    GPIOx_MaskINTConfig(grp, pin_bit, ENABLE);
    GPIOx_ClearINTPendingBit(grp, pin_bit);
}

/* Enable the GPIO interrupt for a key */
static void app_key_int_enable(GPIO_TypeDef *grp, uint32_t pin_bit)
{
    GPIOx_MaskINTConfig(grp, pin_bit, DISABLE);
    GPIOx_INTConfig(grp, pin_bit, ENABLE);
}

/* Interrupt top half: record the level and start the debounce timer */
static void app_key_isr(app_key_button_t *key)
{
    GPIO_TypeDef *grp = app_key_group(key->pin);
    uint32_t pin_bit = GPIO_GetPin(key->pin);

    app_key_int_disable(grp, pin_bit);
    key->raw_level = GPIOx_ReadInputDataBit(grp, pin_bit);
    os_timer_start(&key->debounce_timer);
}

/* Update key state and timestamp after debounce confirmation */
static void app_key_debounce(app_key_button_t *key)
{
    GPIO_TypeDef *grp = app_key_group(key->pin);
    uint32_t pin_bit = GPIO_GetPin(key->pin);

    /* Re-sample; if it differs from the level at interrupt entry, treat as
     * bounce, re-enable the interrupt and return */
    if (key->raw_level != GPIOx_ReadInputDataBit(grp, pin_bit))
    {
        app_key_int_enable(grp, pin_bit);
        return;
    }

    if (key->raw_level == SET)   /* High level = release (PULL_UP, active low) */
    {
        grp->INTPOLARITY &= ~pin_bit;              /* Next capture is press (low) */

        uint32_t now = os_sys_time_get();
        uint32_t duration = now - *(key->out_press);

        /* Only 2 physical keys are wired up but the GUI needs 4 logical
         * keys (Home/Back/Menu/Power). A hold >= APP_KEY_LONG_PRESS_MS is
         * forwarded as a synthetic short click of the remapped key: this
         * key's own out_release is left untouched (so kb_algo_process()
         * does not also fire an event for it), only out_state is cleared
         * to reflect the physical release. */
        if (duration >= APP_KEY_LONG_PRESS_MS && key->remap_state != NULL)
        {
            *(key->out_state)      = false;
            *(key->remap_press)    = now - 1;
            *(key->remap_state)    = false;
            *(key->remap_release)  = os_sys_time_get();
            APP_KEY_LOG("[app_key] pin %d long-press %d ms -> remap", key->pin, duration);
        }
        else
        {
            *(key->out_state)   = false;
            *(key->out_release) = now;
            APP_KEY_LOG("[app_key] pin %d release @ %d", key->pin, *(key->out_release));
        }
    }
    else                         /* Low level = press */
    {
        grp->INTPOLARITY |= pin_bit;               /* Next capture is release (high) */
        *(key->out_state) = true;
        *(key->out_press) = os_sys_time_get();
        APP_KEY_LOG("[app_key] pin %d press @ %d", key->pin, *(key->out_press));
    }

    app_key_int_enable(grp, pin_bit);
}

static void app_key_debounce_power(void *timer)
{
    (void)timer;
    app_key_debounce(&s_keys[APP_KEY_POWER]);
}

static void app_key_debounce_home(void *timer)
{
    (void)timer;
    app_key_debounce(&s_keys[APP_KEY_HOME]);
}

/* Override the __weak symbol in vector_table.h */
void GPIOA2_Handler(void)
{
    app_key_isr(&s_keys[APP_KEY_POWER]);
}

void GPIOB6_Handler(void)
{
    app_key_isr(&s_keys[APP_KEY_HOME]);
}

static void app_key_pad_init(uint8_t pin)
{
    Pinmux_Config(pin, DWGPIO);
    Pad_Config(pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
}

static void app_key_gpio_init(uint8_t pin, IRQn_Type irqn, VECTORn_Type vector,
                              IRQ_Fun handler)
{
    GPIO_TypeDef *grp = app_key_group(pin);

    if (grp == GPIOA)
    {
        RCC_PeriphClockCmd(APBPeriph_GPIOA, APBPeriph_GPIOA_CLOCK, ENABLE);
    }
    else
    {
        RCC_PeriphClockCmd(APBPeriph_GPIOB, APBPeriph_GPIOB_CLOCK, ENABLE);
    }

    RamVectorTableUpdate(vector, handler);

    GPIO_InitTypeDef gpio_param;
    GPIO_StructInit(&gpio_param);
    gpio_param.GPIO_PinBit     = GPIO_GetPin(pin);
    gpio_param.GPIO_Mode       = GPIO_Mode_IN;
    gpio_param.GPIO_ITCmd      = ENABLE;
    gpio_param.GPIO_ITTrigger  = GPIO_INT_Trigger_LEVEL;
    gpio_param.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_LOW;
    GPIOx_Init(grp, &gpio_param);

    NVIC_InitTypeDef nvic_param;
    nvic_param.NVIC_IRQChannel         = irqn;
    nvic_param.NVIC_IRQChannelPriority = 3;
    nvic_param.NVIC_IRQChannelCmd      = ENABLE;
    NVIC_Init(&nvic_param);
}

void app_key_button_init(void)
{
    /* Physical "Power" button: short press -> Menu (more frequently used),
     * long press -> Power (single, infrequent action). */
    s_keys[APP_KEY_POWER].pin           = ADC_2;   /* GPIOA2 -> Menu (long press -> Power) */
    s_keys[APP_KEY_POWER].out_state     = &menu_state;
    s_keys[APP_KEY_POWER].out_press     = &menu_timestamp_ms_press;
    s_keys[APP_KEY_POWER].out_release   = &menu_timestamp_ms_release;
    s_keys[APP_KEY_POWER].remap_state   = &power_state;
    s_keys[APP_KEY_POWER].remap_press   = &power_timestamp_ms_press;
    s_keys[APP_KEY_POWER].remap_release = &power_timestamp_ms_release;

    s_keys[APP_KEY_HOME].pin            = P3_5;    /* GPIOB6 -> Home (long press -> Back) */
    s_keys[APP_KEY_HOME].out_state      = &home_state;
    s_keys[APP_KEY_HOME].out_press      = &home_timestamp_ms_press;
    s_keys[APP_KEY_HOME].out_release    = &home_timestamp_ms_release;
    s_keys[APP_KEY_HOME].remap_state    = &back_state;
    s_keys[APP_KEY_HOME].remap_press    = &back_timestamp_ms_press;
    s_keys[APP_KEY_HOME].remap_release  = &back_timestamp_ms_release;

    app_key_pad_init(ADC_2);
    app_key_pad_init(P3_5);

    os_timer_create(&s_keys[APP_KEY_POWER].debounce_timer, "key_power_deb",
                    1, APP_KEY_DEBOUNCE_TIME_MS, false, app_key_debounce_power);
    os_timer_create(&s_keys[APP_KEY_HOME].debounce_timer, "key_home_deb",
                    2, APP_KEY_DEBOUNCE_TIME_MS, false, app_key_debounce_home);

    app_key_gpio_init(ADC_2, GPIO2_IRQn,  GPIOA2_VECTORn, (IRQ_Fun)GPIOA2_Handler);
    app_key_gpio_init(P3_5,  GPIO38_IRQn, GPIOB6_VECTORn, (IRQ_Fun)GPIOB6_Handler);

    /* Enable interrupts (polarity is active-low now, waiting for first press) */
    app_key_int_enable(GPIOA, GPIO_GetPin(ADC_2));
    app_key_int_enable(GPIOB, GPIO_GetPin(P3_5));
}
