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
#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_touch.h"
#include <os_task.h>

#define TOUCH_DEVICE_PATH           "/dev/touch0"
#define TOUCH_PANEL_WIDTH           360
#define TOUCH_PANEL_HEIGHT          360
#define TOUCH_READER_STACK_SIZE     2048
#define TOUCH_READER_PRIORITY       2
#define TOUCH_RETRY_DELAY_MS        100

/* Written by touch_reader_task and copied by port_touchpad_get_data(). */
static gui_touch_port_data_t touch_latest = { .event = GUI_TOUCH_EVENT_UP };
static gui_touch_port_data_t touch_snapshot = { .event = GUI_TOUCH_EVENT_UP };

static gui_wheel_port_data_t wheel_port_data = {0};

// Keyboard state variables
static bool home_state = false;
static uint32_t home_timestamp_ms_press = 0;
static uint32_t home_timestamp_ms_release = 0;


/***touch device***/
static uint16_t touch_clamp_coordinate(uint16_t coordinate, uint16_t max)
{
    return coordinate < max ? coordinate : (uint16_t)(max - 1U);
}

static void touch_update_latest(const posix_touch_data_t *touch_data,
                                bool pressed)
{
    uint32_t timestamp_ms = gui_ms_get();
    uint32_t key = os_lock();

    if (pressed)
    {
        uint16_t x = touch_clamp_coordinate(touch_data->points[0].x,
                                            TOUCH_PANEL_WIDTH);
        uint16_t y = touch_clamp_coordinate(touch_data->points[0].y,
                                            TOUCH_PANEL_HEIGHT);

        if (touch_latest.event != GUI_TOUCH_EVENT_DOWN)
        {
            touch_latest.x_coordinate_start = x;
            touch_latest.y_coordinate_start = y;
            touch_latest.timestamp_ms_start = timestamp_ms;
        }

        touch_latest.x_coordinate = x;
        touch_latest.y_coordinate = y;
        touch_latest.event = GUI_TOUCH_EVENT_DOWN;
    }
    else
    {
        touch_latest.event = GUI_TOUCH_EVENT_UP;
    }

    touch_latest.timestamp_ms = timestamp_ms;
    os_unlock(key);
}

static void touch_reader_task(void *context)
{
    (void)context;

    posix_fd_t fd = POSIX_FD_NULL;
    uint32_t retry_log_ms = 0;
    bool opened = false;

    while (!opened)
    {
        (void)posix_port_init_all();
        fd = posix_open(TOUCH_DEVICE_PATH);
        if (fd != POSIX_FD_NULL)
        {
            opened = true;
            break;
        }

        uint32_t now_ms = gui_ms_get();
        if ((now_ms - retry_log_ms) >= 1000U)
        {
            retry_log_ms = now_ms;
            gui_log("touch: open %s failed, retrying", TOUCH_DEVICE_PATH);
        }
        os_delay(TOUCH_RETRY_DELAY_MS);
    }

    posix_touch_config_t cfg =
    {
        .width = TOUCH_PANEL_WIDTH,
        .height = TOUCH_PANEL_HEIGHT,
        .swap_xy = 0,
    };
    if (posix_ioctl(fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg) != POSIX_OK)
    {
        gui_log("touch: set config failed");
    }

    for (;;)
    {
        posix_touch_data_t touch_data = {0};
        posix_ssize_t ret = posix_read(fd, &touch_data, sizeof(touch_data));

        if (ret == POSIX_ERR_TIMEOUT)
        {
            touch_update_latest(&touch_data, false);
            continue;
        }
        if (ret < 0)
        {
            gui_log("touch: read failed (%d)", (int)ret);
            os_delay(20);
            continue;
        }

        bool pressed = touch_data.point_count > 0 &&
                       touch_data.points[0].status == POSIX_TOUCH_PRESS;
        touch_update_latest(&touch_data, pressed);
    }
}

gui_touch_port_data_t *port_touchpad_get_data(void)
{
    uint32_t timestamp_ms = gui_ms_get();
    uint32_t key = os_lock();
    touch_snapshot = touch_latest;
    if (touch_snapshot.event == GUI_TOUCH_EVENT_DOWN)
    {
        touch_snapshot.timestamp_ms = timestamp_ms;
    }
    os_unlock(key);
    return &touch_snapshot;
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

    .tp_witdh = TOUCH_PANEL_WIDTH,
    .tp_height = TOUCH_PANEL_HEIGHT,
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
    gui_log("gui_port_indev_init - touch only");

    gui_indev_info_register(&indev);

    void *touch_task = NULL;
    if (!os_task_create(&touch_task, "touch", touch_reader_task, NULL,
                        TOUCH_READER_STACK_SIZE, TOUCH_READER_PRIORITY))
    {
        gui_log("touch: reader task creation failed");
    }

    // Create keyboard input devices using the new API
    gui_kb_create("Home", &home_state,
                  &home_timestamp_ms_press,
                  &home_timestamp_ms_release);
}
