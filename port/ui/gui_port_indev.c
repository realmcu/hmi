#include "guidef.h"
#include "gui_port.h"
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
#include <zephyr/sys/reboot.h>
#include <pm.h>
#include <indirect_access.h>
#include "rtl876x_pinmux.h"
#include <power_manager_unit_platform.h>

#define TOUCH_DEV_NODE          DT_NODELABEL(touch_device)
#define POWER_KEY_NODE          DT_NODELABEL(key1)
#define POWER_LONG_PRESS_MS     3000U
#define POWER_OFF_AON_REG       0x6U
#define POWER_OFF_AON_MAGIC     0xEB3AU

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
static bool power_off_requested = false;

extern void rtk_lcd_hal_set_display(bool on);

static void wireless_power_set(bool on)
{
    bt_power_mode_set(on ? BTPOWER_ACTIVE : BTPOWER_DEEP_SLEEP);
    Pad_Config(ADC_2, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE,
               PAD_OUT_ENABLE, on ? PAD_OUT_HIGH : PAD_OUT_LOW);
    Pad_Config(P2_6, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE,
               PAD_OUT_ENABLE, on ? PAD_OUT_HIGH : PAD_OUT_LOW);
}

static void exit_power_off(void)
{
    btaon_fast_write_safe(POWER_OFF_AON_REG, 0);
    printk("[power-key] power on accepted, rebooting app\n");
    sys_reboot(SYS_REBOOT_COLD);
}

static void enter_power_off(void)
{
    if (power_off_requested)
    {
        return;
    }
    power_off_requested = true;

    printk("[power-key] power off requested\n");

    void *gui_thread = gui_server_get_thread_handle();
    if (gui_thread == NULL || !gui_thread_suspend(gui_thread))
    {
        printk("[power-key] failed to suspend GUI thread\n");
        power_off_requested = false;
        return;
    }

    wireless_power_set(false);
    btaon_fast_write_safe(POWER_OFF_AON_REG, POWER_OFF_AON_MAGIC);
    rtk_lcd_hal_set_display(false);
    printk("[power-key] GUI suspended, entering power-down\n");

    int32_t set_ret = power_mode_set(POWER_POWERDOWN_MODE);
    int32_t resume_ret = power_mode_resume();
    printk("[power-key] pm set=%d resume=%d\n", set_ret, resume_ret);

    if (set_ret != 0 || resume_ret != 0)
    {
        printk("[power-key] power-down request failed\n");
        power_off_requested = false;
        (void)power_mode_set(POWER_ACTIVE_MODE);
        if (resume_ret == 0)
        {
            (void)power_mode_pause();
        }
        wireless_power_set(true);
        rtk_lcd_hal_set_display(true);
        (void)gui_thread_resume(gui_thread);
    }
}

bool gui_port_power_on_gate(void)
{
    uint16_t marker = btaon_fast_read_safe(POWER_OFF_AON_REG);
    PlatformWakeupReason reason = platform_pm_get_wakeup_reason();

    printk("[power-key] boot marker=0x%04x reason=0x%08x key=%d\n", marker,
           (uint32_t)reason,
           gpio_is_ready_dt(&power_key) ? gpio_pin_get_dt(&power_key) : -1);
    if (marker != POWER_OFF_AON_MAGIC)
    {
        return true;
    }

    if (reason != PLATFORM_PM_WAKEUP_GPIO)
    {
        printk("[power-key] cold boot: clearing stale marker\n");
        btaon_fast_write_safe(POWER_OFF_AON_REG, 0);
        return true;
    }

    if (!gpio_is_ready_dt(&power_key) || gpio_pin_get_dt(&power_key) <= 0)
    {
        printk("[power-key] wake press released before gate\n");
        return false;
    }

    uint32_t start_ms = k_uptime_get_32();
    while (gpio_pin_get_dt(&power_key) > 0)
    {
        uint32_t elapsed = k_uptime_get_32() - start_ms;
        if (elapsed >= POWER_LONG_PRESS_MS)
        {
            btaon_fast_write_safe(POWER_OFF_AON_REG, 0);
            printk("[power-key] power on accepted after %u ms, rebooting app\n", elapsed);
            sys_reboot(SYS_REBOOT_COLD);
        }
        k_sleep(K_MSEC(10));
    }

    printk("[power-key] power on rejected: short press\n");
    return false;
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
        if (power_off_requested)
        {
            /* Keep the wake press private until its duration is known. */
            home_state = false;
            home_timestamp_ms_press = key.press_timestamp;
            home_timestamp_ms_release = key.press_timestamp;
            printk("[power-key] wake press\n");
            return;
        }

        home_state = true;
        home_timestamp_ms_press = key.press_timestamp;
        // gui_log("key id %d state %d press %d release %d", key.key_id, key.current_state,
        //         key.press_timestamp, key.release_timestamp);
    }
    else if (GPIO_KEY_RELEASED == key.current_state)
    {
        uint32_t duration = key.release_timestamp - key.press_timestamp;

        home_timestamp_ms_press = key.press_timestamp;
        if (power_off_requested)
        {
            home_timestamp_ms_release = key.press_timestamp;
            home_state = false;
            printk("[power-key] wake release %u ms\n", duration);
            if (duration >= POWER_LONG_PRESS_MS)
            {
                exit_power_off();
            }
            return;
        }

        if (duration >= POWER_LONG_PRESS_MS)
        {
            /* Publish an invalid release before publishing state=false, so
             * HoneyGUI cannot race this callback and enqueue a key event. */
            home_timestamp_ms_release = key.press_timestamp;
            home_state = false;
            printk("[power-key] long release %u ms\n", duration);
            enter_power_off();
            return;
        }

        home_timestamp_ms_release = key.release_timestamp;
        home_state = false;
        printk("[power-key] short release %u ms\n", duration);
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
