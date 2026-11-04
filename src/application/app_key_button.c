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
#include "hal_gpio.h"
#include "hal_gpio_int.h"
#include "mfb_api.h"
#include "section.h"

#define APP_KEY_BUTTON_DEBUG

#ifdef APP_KEY_BUTTON_DEBUG
#define APP_KEY_LOG(...)   DBG_DIRECT(__VA_ARGS__)
#else
#define APP_KEY_LOG(...)
#endif

#define POWER_KEY           ADC_2
#define POWER_KEY_IRQn      GPIO2_IRQn
#define POWER_KEY_VECn      GPIOA2_VECTORn
#define POWER_KEY_HANDLER   GPIOA2_Handler

#define MENU_KEY            P2_1
#define MENU_KEY_IRQn       GPIO16_IRQn
#define MENU_KEY_VECn       GPIOA16_VECTORn
#define MENU_KEY_HANDLER    GPIOA16_Handler

#define HOME_KEY            P3_5
#define HOME_KEY_IRQn       GPIO38_IRQn
#define HOME_KEY_VECn       GPIOB6_VECTORn
#define HOME_KEY_HANDLER    GPIOB6_Handler

/* Software debounce duration (ms), matches Wearable key_button_8773e.c */
#define APP_KEY_DEBOUNCE_TIME_MS    20

#define APP_KEY_TIMER_MFB_DEBOUNCE  1

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

    bool      *out_state;       /* -> xxx_state */
    uint32_t  *out_press;       /* -> xxx_timestamp_ms_press */
    uint32_t  *out_release;     /* -> xxx_timestamp_ms_release */
} app_key_button_t;

enum
{
    APP_KEY_POWER = 0,
    APP_KEY_HOME,
    APP_KEY_MENU,
    APP_KEY_BACK,
    APP_KEY_NUM,
};

static app_key_button_t s_keys[APP_KEY_NUM];

static GPIO_TypeDef *app_key_group(uint8_t pin)
{
    return (GPIO_GetNum(pin) <= GPIO31) ? GPIOA : GPIOB;
}

static void app_key_int_disable(GPIO_TypeDef *grp, uint32_t pin_bit)
{
    GPIOx_INTConfig(grp, pin_bit, DISABLE);
    GPIOx_MaskINTConfig(grp, pin_bit, ENABLE);
    GPIOx_ClearINTPendingBit(grp, pin_bit);
}

static void app_key_int_enable(GPIO_TypeDef *grp, uint32_t pin_bit)
{
    GPIOx_MaskINTConfig(grp, pin_bit, DISABLE);
    GPIOx_INTConfig(grp, pin_bit, ENABLE);
}

static app_key_button_t *app_key_lookup(uint8_t pin)
{
    uint32_t i;

    for (i = 0; i < APP_KEY_NUM; i++)
    {
        if (s_keys[i].out_state != NULL && s_keys[i].pin == pin)
        {
            return &s_keys[i];
        }
    }

    return NULL;
}

/* Interrupt handler: registered per pin with the pin number as `context` */
static void app_key_isr(uint32_t context)
{
    uint8_t           pin = (uint8_t)context;
    app_key_button_t *key = app_key_lookup(pin);
    T_GPIO_LEVEL      gpio_level;

    if (key == NULL)
    {
        return;
    }

    gpio_level = hal_gpio_get_input_level(pin);

    hal_gpio_irq_disable(pin);

    if (gpio_level == GPIO_LEVEL_LOW)
    {
        hal_gpio_irq_change_polarity(pin, GPIO_IRQ_ACTIVE_HIGH);
        *(key->out_state) = true;
        *(key->out_press) = os_sys_time_get();
    }
    else
    {
        hal_gpio_irq_change_polarity(pin, GPIO_IRQ_ACTIVE_LOW);
        *(key->out_state) = false;
        *(key->out_release) = os_sys_time_get();
    }

    hal_gpio_irq_enable(pin);
}

static void app_key_pad_init(uint8_t pin)
{
    Pinmux_Config(pin, DWGPIO);
    Pad_Config(pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
}

static bool  s_mfb_raw_pressed;
static bool  s_mfb_pressed;
static void *s_mfb_debounce_timer;

ISR_TEXT_SECTION
static void app_key_mfb_isr(void)
{
    mfb_irq_disable();

    s_mfb_raw_pressed = !mfb_get_level();
    os_timer_start(&s_mfb_debounce_timer);
}

static void app_key_mfb_debounce(void *timer)
{
    bool     pressed;
    uint32_t now;

    (void)timer;

    pressed = !mfb_get_level();

    if (pressed != s_mfb_raw_pressed)
    {
        mfb_irq_enable();
        return;
    }

    if (pressed != s_mfb_pressed)
    {
        s_mfb_pressed = pressed;
        *(s_keys[APP_KEY_BACK].out_state) = pressed;
        now = os_sys_time_get();
        if (pressed)
        {
            *(s_keys[APP_KEY_BACK].out_press) = now;
        }
        else
        {
            *(s_keys[APP_KEY_BACK].out_release) = now;
        }
    }
    mfb_irq_enable();
}

void app_key_mfb_init(void)
{
    s_mfb_pressed = !mfb_get_level();

    os_timer_create(&s_mfb_debounce_timer, "mfb_deb", APP_KEY_TIMER_MFB_DEBOUNCE,
                    APP_KEY_DEBOUNCE_TIME_MS, false, app_key_mfb_debounce);

    mfb_init(app_key_mfb_isr);
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
    gpio_param.GPIO_ITTrigger  = GPIO_INT_Trigger_EDGE;
    gpio_param.GPIO_ITPolarity = GPIO_INT_POLARITY_ACTIVE_LOW;
    gpio_param.GPIO_DebounceTime = APP_KEY_DEBOUNCE_TIME_MS;
    gpio_param.GPIO_ITDebounce = GPIO_INT_DEBOUNCE_ENABLE;
    GPIOx_Init(grp, &gpio_param);

    NVIC_InitTypeDef nvic_param;
    nvic_param.NVIC_IRQChannel         = irqn;
    nvic_param.NVIC_IRQChannelPriority = 3;
    nvic_param.NVIC_IRQChannelCmd      = ENABLE;
    NVIC_Init(&nvic_param);
}

void app_key_button_init(void)
{
    s_keys[APP_KEY_POWER].pin           = POWER_KEY;
    s_keys[APP_KEY_POWER].out_state     = &power_state;
    s_keys[APP_KEY_POWER].out_press     = &power_timestamp_ms_press;
    s_keys[APP_KEY_POWER].out_release   = &power_timestamp_ms_release;

    s_keys[APP_KEY_HOME].pin            = HOME_KEY;
    s_keys[APP_KEY_HOME].out_state      = &home_state;
    s_keys[APP_KEY_HOME].out_press      = &home_timestamp_ms_press;
    s_keys[APP_KEY_HOME].out_release    = &home_timestamp_ms_release;

    s_keys[APP_KEY_MENU].pin            = MENU_KEY;
    s_keys[APP_KEY_MENU].out_state      = &menu_state;
    s_keys[APP_KEY_MENU].out_press      = &menu_timestamp_ms_press;
    s_keys[APP_KEY_MENU].out_release    = &menu_timestamp_ms_release;

    s_keys[APP_KEY_BACK].out_state      = &back_state;
    s_keys[APP_KEY_BACK].out_press      = &back_timestamp_ms_press;
    s_keys[APP_KEY_BACK].out_release    = &back_timestamp_ms_release;

    hal_gpio_init();
    hal_gpio_int_init();
    hal_gpio_set_debounce_time(30);

    hal_gpio_init_pin(POWER_KEY, GPIO_TYPE_AUTO, GPIO_DIR_INPUT, GPIO_PULL_UP);
    hal_gpio_init_pin(MENU_KEY, GPIO_TYPE_AUTO, GPIO_DIR_INPUT, GPIO_PULL_UP);
    hal_gpio_init_pin(HOME_KEY, GPIO_TYPE_AUTO, GPIO_DIR_INPUT, GPIO_PULL_UP);

    hal_gpio_set_up_irq(POWER_KEY, GPIO_IRQ_EDGE, GPIO_IRQ_ACTIVE_LOW, true);
    hal_gpio_set_up_irq(MENU_KEY, GPIO_IRQ_EDGE, GPIO_IRQ_ACTIVE_LOW, true);
    hal_gpio_set_up_irq(HOME_KEY, GPIO_IRQ_EDGE, GPIO_IRQ_ACTIVE_LOW, true);


    hal_gpio_register_isr_callback(POWER_KEY, app_key_isr, POWER_KEY);
    hal_gpio_register_isr_callback(MENU_KEY, app_key_isr, MENU_KEY);
    hal_gpio_register_isr_callback(HOME_KEY, app_key_isr, HOME_KEY);

    /* Enable interrupts (polarity is active-low now, waiting for first press) */
    app_key_int_enable(app_key_group(POWER_KEY), GPIO_GetPin(POWER_KEY));
    app_key_int_enable(app_key_group(HOME_KEY), GPIO_GetPin(HOME_KEY));
    app_key_int_enable(app_key_group(MENU_KEY), GPIO_GetPin(MENU_KEY));

    app_key_mfb_init();
}
