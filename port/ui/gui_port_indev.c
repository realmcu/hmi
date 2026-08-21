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

#include "touch_cst820_zephyr.h"
#include "key_button_8773g_zephyr.h"

#define TOUCH_DEV_NODE  DT_NODELABEL(touch_device)

static const struct device *touch_dev = DEVICE_DT_GET(TOUCH_DEV_NODE);
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio_keys));
static uint16_t key1 = DT_PROP(DT_NODELABEL(key1), key_id);


static gui_touch_port_data_t raw_data = {0};

static gui_wheel_port_data_t wheel_port_data = {0};

// Keyboard state variables
static bool home_state = false;
static uint32_t home_timestamp_ms_press = 0;
static uint32_t home_timestamp_ms_release = 0;


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
        home_state = true;
        home_timestamp_ms_press = key.press_timestamp;
        // gui_log("key id %d state %d press %d release %d", key.key_id, key.current_state,
        //         key.press_timestamp, key.release_timestamp);
    }
    else if (GPIO_KEY_RELEASED == key.current_state)
    {
        home_state = false;
        home_timestamp_ms_press = key.press_timestamp;
        home_timestamp_ms_release = key.release_timestamp;
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
    // gui_log("gpio_button_callback");

    kb_get_data();

    // T_GPIO_KEY *key_btn = (T_GPIO_KEY *)key;
    // if (key_btn->current_state == GPIO_KEY_RELEASED)
    // {
    //     gui_msg_t msg;
    //     msg.event = GUI_EVENT_DISPLAY_ON;
    //     gui_send_msg_to_server(&msg);
    // }
}

extern void gui_indev_info_register(struct gui_indev *info);

void gui_port_indev_init(void)
{
    gui_log("gui_port_indev_init - touch only");

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
