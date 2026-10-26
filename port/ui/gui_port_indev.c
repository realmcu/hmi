/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "guidef.h"
#include "gui_port.h"
#include "os_sched.h"
#include "os_sync.h"
#include "wdg.h"
#include "kb_algo.h"
#include "wheel_algo.h"
#include "tp_algo.h"
#include "gui_api.h"
#include "trace.h"
#include "gui_message.h"
#include "key_button_8773g_zephyr.h"
#include "mfb_api.h"

#include <zephyr/kernel.h>

#define GPIO_KEYS_NODE  DT_NODELABEL(gpio_keys)

/* key,id of every gpio-keys child, see boards/rtl87x3g_evb.overlay. */
#define KEY_ID_HOME     DT_PROP(DT_NODELABEL(key1), key_id)
#define KEY_ID_POWER    DT_PROP(DT_NODELABEL(key2), key_id)
#define KEY_ID_MENU     DT_PROP(DT_NODELABEL(key3), key_id)

/* Debounce time of the MFB key, the gpio-keys driver uses 30 ms as well. */
#define MFB_DEBOUNCE_MS 30

static const struct device *gpio_keys_dev = DEVICE_DT_GET(GPIO_KEYS_NODE);

/*
 * The touch panel was removed (see boards/rtl87x3g_evb.overlay), so the port
 * always reports "no touch": tp_algo_process() asserts on a NULL pointer, it
 * cannot be left out, and GUI_TOUCH_EVENT_NONE == 0 so a zeroed struct means
 * "finger up" forever.
 */
static gui_touch_port_data_t raw_data = {0};

static gui_wheel_port_data_t wheel_port_data = {0};

/*
 * A gpio key is reported by the realtek gpio-keys driver, while the MFB
 * (multi function button) sits on a dedicated pad which is not part of the
 * GPIO controller, it is reported by the mfb interrupt.
 */
typedef enum
{
    KEY_SOURCE_GPIO,
    KEY_SOURCE_MFB,
} T_KEY_SOURCE;

/*
 * One entry per gui keyboard device. The state and timestamp members are
 * handed over to gui_kb_create() as pointers, so entries must never move
 * once the keyboard devices have been created.
 */
typedef struct
{
    const char *name;
    T_KEY_SOURCE source;
    uint32_t key_id;            /* gpio-keys id, only used for KEY_SOURCE_GPIO */
    bool state;
    uint32_t timestamp_ms_press;
    uint32_t timestamp_ms_release;
} T_APP_KEY;

static T_APP_KEY app_keys[] =
{
    { .name = "Home",  .source = KEY_SOURCE_GPIO, .key_id = KEY_ID_HOME  },
    { .name = "Back",  .source = KEY_SOURCE_MFB,  .key_id = 0            },
    { .name = "Menu",  .source = KEY_SOURCE_GPIO, .key_id = KEY_ID_MENU  },
    { .name = "Power", .source = KEY_SOURCE_GPIO, .key_id = KEY_ID_POWER },
};

static struct k_work_delayable mfb_debounce_work;
static bool mfb_pressed;

/***touch device***/
/* Touch panel removed: keep the interface, report "no touch" forever. */
gui_touch_port_data_t *port_touchpad_get_data()
{
    return &raw_data;

}

/***kb device***/
void port_button_set_indicate(void (*callback)(void))
{
    return;
}

static T_APP_KEY *app_key_find_by_id(uint32_t key_id)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(app_keys); i++)
    {
        if ((app_keys[i].source == KEY_SOURCE_GPIO) && (app_keys[i].key_id == key_id))
        {
            return &app_keys[i];
        }
    }

    return NULL;
}

static T_APP_KEY *app_key_find_by_source(T_KEY_SOURCE source)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(app_keys); i++)
    {
        if (app_keys[i].source == source)
        {
            return &app_keys[i];
        }
    }

    return NULL;
}

/*
 * Publish one press/release edge to the gui keyboard device. kb_algo only
 * fires an event when the release timestamp is newer than the press timestamp,
 * so both timestamps have to come from the same time base.
 */
static void app_key_report(T_APP_KEY *key, bool pressed, uint32_t press_ms, uint32_t release_ms)
{
    if (key == NULL)
    {
        return;
    }

    if (pressed)
    {
        if (key->state)
        {
            return;
        }

        key->state = true;
        key->timestamp_ms_press = press_ms;

        DBG_DIRECT("[KEY] %s pressed (ts %u)\n", key->name, (unsigned int)press_ms);

        /* Any key press wakes up the panel and restarts the gui idle timer. */
        gui_msg_t msg = { .event = GUI_EVENT_DISPLAY_ON };
        gui_send_msg_to_server(&msg);
    }
    else
    {
        if (!key->state)
        {
            return;
        }

        key->state = false;
        key->timestamp_ms_release = release_ms;

        DBG_DIRECT("[KEY] %s released, duration %u ms (ts %u)\n", key->name,
                   (unsigned int)(release_ms - key->timestamp_ms_press), (unsigned int)release_ms);
    }
}

/* Runs in the system work queue, the gpio-keys driver debounces the edges. */
static void gpio_key_callback(T_GPIO_KEY *key)
{
    if (key == NULL)
    {
        return;
    }

    T_APP_KEY *app_key = app_key_find_by_id(key->key_id);
    if (app_key == NULL)
    {
        DBG_DIRECT("[KEY] unexpected gpio key id %u\n", (unsigned int)key->key_id);
        return;
    }

    if (key->current_state == GPIO_KEY_PRESSED)
    {
        app_key_report(app_key, true, key->press_timestamp, key->release_timestamp);
    }
    else if (key->current_state == GPIO_KEY_RELEASED)
    {
        app_key_report(app_key, false, key->press_timestamp, key->release_timestamp);
    }
}

/*
 * MFB (Back key) handling.
 *
 * The MFB interrupt callback runs in interrupt context, so the level is only
 * re-read after a debounce delay in the system work queue, which keeps this
 * path symmetric with the gpio-keys driver.
 */
static void mfb_debounce_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    /* MFB is active low: a low level means the button is pressed. */
    bool pressed = !mfb_get_level();

    if (pressed != mfb_pressed)
    {
        uint32_t now_ms = k_uptime_get_32();

        mfb_pressed = pressed;
        app_key_report(app_key_find_by_source(KEY_SOURCE_MFB), pressed, now_ms, now_ms);
    }

    mfb_irq_enable();
}

static void mfb_level_change_callback(void)
{
    mfb_irq_disable();
    k_work_reschedule(&mfb_debounce_work, K_MSEC(MFB_DEBOUNCE_MS));
}

/***wheel device***/
//todo
gui_wheel_port_data_t *port_wheel_get_data(void)
{
    return &wheel_port_data;
}

static struct gui_indev indev =
{
    .tp_get_data = port_touchpad_get_data,
    .wheel_get_port_data = port_wheel_get_data,

    .touch_timeout_ms = 30,
    .long_button_time_ms = 800,
    .short_button_time_ms = 300,
    .quick_slide_time_ms = 50,

    .kb_long_button_time_ms = 2000,
    .kb_short_button_time_ms = 60,

};

extern void gui_indev_info_register(struct gui_indev *info);
void gui_port_indev_init(void)
{
    gui_indev_info_register(&indev);

    DBG_DIRECT("[KEY] gpio_keys device %s, ready %d\n", gpio_keys_dev->name,
               device_is_ready(gpio_keys_dev) ? 1 : 0);

    /* GPIO keys, one driver callback per key id. */
    for (uint32_t i = 0; i < ARRAY_SIZE(app_keys); i++)
    {
        if (app_keys[i].source != KEY_SOURCE_GPIO)
        {
            continue;
        }

        int32_t ret = gpio_button_register_callback(gpio_keys_dev, (uint16_t)app_keys[i].key_id,
                                                    gpio_key_callback);
        if (ret != 0)
        {
            DBG_DIRECT("[KEY] %s: register key id %u FAILED (%d)\n", app_keys[i].name,
                       (unsigned int)app_keys[i].key_id, (int)ret);
        }
        else
        {
            DBG_DIRECT("[KEY] %s: gpio key id %u registered\n", app_keys[i].name,
                       (unsigned int)app_keys[i].key_id);
        }
    }

    /* MFB key, only reachable through the MFB API. */
    k_work_init_delayable(&mfb_debounce_work, mfb_debounce_work_handler);
    mfb_pressed = false;
    mfb_init(mfb_level_change_callback);
    mfb_irq_enable();
    DBG_DIRECT("[KEY] %s: mfb key registered\n", app_key_find_by_source(KEY_SOURCE_MFB)->name);

    for (uint32_t i = 0; i < ARRAY_SIZE(app_keys); i++)
    {
        gui_kb_create((char *)app_keys[i].name, &app_keys[i].state,
                      &app_keys[i].timestamp_ms_press,
                      &app_keys[i].timestamp_ms_release);
        DBG_DIRECT("[KEY] %s: gui keyboard device created\n", app_keys[i].name);
    }
}
