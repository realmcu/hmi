#include "guidef.h"
#include "gui_port.h"
#include "app_power_manage.h"
#include "os_sched.h"
#include "os_sync.h"
#include "wdg.h"
#include "kb_algo.h"
#include "wheel_algo.h"
#include "tp_algo.h"
#include "gui_api.h"
#include "gui_api_os.h"
#include "gui_server.h"
#include "trace.h"
#include "gui_message.h"

#include "touch_cst820_zephyr.h"
#include "key_button_8773g_zephyr.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define TOUCH_DEV_NODE          DT_NODELABEL(touch_device)
#define POWER_KEY_NODE          DT_NODELABEL(key1)

static const struct device *touch_dev = DEVICE_DT_GET(TOUCH_DEV_NODE);
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio_keys));
static const struct gpio_dt_spec power_key = GPIO_DT_SPEC_GET(POWER_KEY_NODE, gpios);
static uint16_t key1 = DT_PROP(POWER_KEY_NODE, key_id);

static gui_touch_port_data_t raw_data = {0};

static gui_wheel_port_data_t wheel_port_data = {0};

// Keyboard state variables
static bool home_state = false;
static uint32_t home_timestamp_ms_press = 0;
static uint32_t home_timestamp_ms_release = 0;
static bool power_key_press_accepted = false;
static bool suppress_power_key_until_release = false;
static struct k_work_delayable power_key_long_work;

static void power_key_long_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!gpio_is_ready_dt(&power_key) || gpio_pin_get_dt(&power_key) <= 0)
    {
        return;
    }

    home_state = false;
    home_timestamp_ms_release = home_timestamp_ms_press;
    power_key_press_accepted = false;
    app_power_manage_handle_long_press();
}

void gui_port_indev_quiesce(void)
{
    static struct k_work_sync sync;

    (void)k_work_cancel_delayable_sync(&power_key_long_work, &sync);
}

/***touch device***/
gui_touch_port_data_t *port_touchpad_get_data()
{
    TOUCH_DATA touch_raw_data;
    bool pressing = 0;
    /*get touch data*/
    uint32_t s = os_lock();
    touch_raw_data = get_raw_touch_data(touch_dev);
    os_unlock(s);

    raw_data.x_coordinate_start = touch_raw_data.x_start;
    raw_data.y_coordinate_start = touch_raw_data.y_start;
    raw_data.timestamp_ms_start = touch_raw_data.timestamp_ms_start;

    raw_data.x_coordinate = touch_raw_data.x;
    raw_data.y_coordinate = touch_raw_data.y;
    raw_data.timestamp_ms = touch_raw_data.timestamp_ms_pressing;

    raw_data.width = 0;
    pressing = touch_raw_data.is_press;

    //gui_log("x %d y %d time %d press %d",raw_data.x_coordinate, raw_data.y_coordinate, raw_data.timestamp_ms, pressing);

    if (pressing == true)
    {
        raw_data.event = GUI_TOUCH_EVENT_DOWN;
    }
    else
    {
        raw_data.event = GUI_TOUCH_EVENT_UP;
    }
    return &raw_data;

}

/***kb device***/
void port_button_set_indicate(void (*callback)(void))
{
    return;
}


void kb_get_data(void)
{
    T_GPIO_KEY key;
    // gui_log("kb_get_data\n");
#if DT_NODE_HAS_STATUS(DT_NODELABEL(key1), okay)
    {
        // gui_log("key1 is okay gpio_button_read_key\n");
        uint32_t s = os_lock();
        key = gpio_button_read_key(gpio_dev, key1);
        os_unlock(s);
    }
#else
    {
        // uint32_t s = os_lock();
        // key = app_mfb_get_level();
        // os_unlock(s);
    }
#endif

    // gui_log("key id %d state %d press %d release %d", key.key_id, key.current_state,
    //         key.press_timestamp, key.release_timestamp);
    if (GPIO_KEY_PRESSED == key.current_state)
    {
        if (suppress_power_key_until_release)
        {
            home_state = false;
            power_key_press_accepted = false;
            // DBG_DIRECT("[power-key] startup press suppressed\n");
            return;
        }

        home_timestamp_ms_press = key.press_timestamp;
        home_timestamp_ms_release = key.press_timestamp;
        home_state = !app_power_power_off_requested();
        power_key_press_accepted = true;
        (void)k_work_reschedule(&power_key_long_work, K_MSEC(APP_POWER_LONG_PRESS_MS));
        // DBG_DIRECT("[power-key] %s press, long action armed\n",
        //        app_power_power_off_requested() ? "wake" : "power-off");
    }
    else if (GPIO_KEY_RELEASED == key.current_state)
    {
        uint32_t duration = key.release_timestamp - key.press_timestamp;

        (void)k_work_cancel_delayable(&power_key_long_work);
        home_state = false;
        if (suppress_power_key_until_release)
        {
            suppress_power_key_until_release = false;
            power_key_press_accepted = false;
            home_timestamp_ms_press = key.release_timestamp;
            home_timestamp_ms_release = key.release_timestamp;
            // DBG_DIRECT("[power-key] startup release suppressed\n");
            return;
        }

        if (!power_key_press_accepted)
        {
            home_timestamp_ms_press = key.release_timestamp;
            home_timestamp_ms_release = key.release_timestamp;
            // DBG_DIRECT("[power-key] unpaired release ignored\n");
            return;
        }
        power_key_press_accepted = false;

        home_timestamp_ms_press = key.press_timestamp;
        if (app_power_power_off_requested() || duration >= APP_POWER_LONG_PRESS_MS)
        {
            home_timestamp_ms_release = key.press_timestamp;
            // DBG_DIRECT("[power-key] release %u ms, no action\n", duration);
            return;
        }

        home_timestamp_ms_release = key.release_timestamp;
        DBG_DIRECT("[power-key] short release %u ms\n", duration);
    }
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

    .touch_timeout_ms = DT_PROP(TOUCH_DEV_NODE, gesture_release_timeout_ms),
    .long_button_time_ms = 800,
    .short_button_time_ms = 300,
    .quick_slide_time_ms = 200,

    .kb_long_button_time_ms = 2000,
    .kb_short_button_time_ms = 60,

};

static void gpio_button_callback(void *key)
{
    // gui_log("gpio_button_callback\n");
    kb_get_data();

    T_GPIO_KEY *key_btn = (T_GPIO_KEY *)key;
    if (key_btn->current_state == GPIO_KEY_RELEASED)
    {
        DBG_DIRECT("gpio_button_callback release\n");

        extern bool is_screenon;
        if (!is_screenon &&
            home_timestamp_ms_release - home_timestamp_ms_press < APP_POWER_LONG_PRESS_MS)
        {
            gui_msg_t msg = {.event = GUI_EVENT_RESET_ACTIVE_TIME};
            gui_send_msg_to_server(&msg);
        }
    }
}

extern void gui_indev_info_register(struct gui_indev *info);

void gui_port_indev_init(void)
{
    gui_log("gui_port_indev_init - touch only");
    k_work_init_delayable(&power_key_long_work, power_key_long_work_handler);
    suppress_power_key_until_release = gpio_is_ready_dt(&power_key) &&
                                       gpio_pin_get_dt(&power_key) > 0;
    if (suppress_power_key_until_release)
    {
        DBG_DIRECT("[power-key] startup key held, suppressing until release\n");
    }

#if DT_NODE_HAS_STATUS(DT_NODELABEL(key1), okay)
    int32_t ret = gpio_button_register_callback(gpio_dev, key1,
                                                (T_GPIO_KEY_CALLBACK)gpio_button_callback);
    if (ret != 0)
    {
        gui_log("gpio_button_register_callback failed dev name %s, key %d", gpio_dev->name, key1);
    }
#endif

    gui_indev_info_register(&indev);

    // Create keyboard input devices using the new API
    gui_kb_create("Home", &home_state,
                  &home_timestamp_ms_press,
                  &home_timestamp_ms_release);
}
