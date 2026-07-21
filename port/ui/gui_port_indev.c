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

/* TODO: 旧的 Zephyr touch driver 已停用（CONFIG_REALTEK_TOUCH_CHSC6417_ZEPHYR=n，
 * DT node touch_device 也已从 overlay 移除）。这里暂时 stub，等 component/posix
 * 里 CHSC6417 的新封装完成后，把 touch 数据源切过来。 */
#include "key_button_8773g_zephyr.h"

static gui_touch_port_data_t raw_data = {0};

static gui_wheel_port_data_t wheel_port_data = {0};

// Keyboard state variables
static bool home_state = false;
static uint32_t home_timestamp_ms_press = 0;
static uint32_t home_timestamp_ms_release = 0;


/***touch device***/
gui_touch_port_data_t *port_touchpad_get_data()
{
    /* TODO: 接回 component/posix 的 touch 封装后，在这里读取 X/Y/pressing。
     * 目前返回上一次（全 0）数据，等价于永远 UP。 */
    raw_data.event = GUI_TOUCH_EVENT_UP;
    return &raw_data;
}

/***kb device***/
void port_button_set_indicate(void (*callback)(void))
{
    return;
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

    .touch_timeout_ms = 30,  /* TODO: 原来取自 DT touch_device.gesture-release-timeout-ms，
                              *       posix 封装接回来后改成从新配置读取 */
    .long_button_time_ms = 800,
    .short_button_time_ms = 300,
    .quick_slide_time_ms = 50,

    .kb_long_button_time_ms = 2000,
    .kb_short_button_time_ms = 60,

};

extern void gui_indev_info_register(struct gui_indev *info);
void gui_port_indev_init(void)
{
    gui_log("gui_port_indev_init - touch only");

    gui_indev_info_register(&indev);
    // Create keyboard input devices using the new API
    gui_kb_create("Home", &home_state,
                  &home_timestamp_ms_press,
                  &home_timestamp_ms_release);
}
