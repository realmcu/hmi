#include "lvgl.h"

#include <string.h>
#include "board.h"
#include "trace.h"
#include "os_sync.h"
#include "os_task.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_demo_benchmark.h"
#include "lv_demo_widgets.h"
#include "os_sched.h"
#include "trace.h"
#include <stdio.h>
#include <stdarg.h>
#include "lv_timer.h"
#include "lv_log.h"

#define APP_TASK_PRIORITY               1   /* Task priorities. */
#define APP_TASK_STACK_SIZE             (512 * 8)

static void port_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[512];
    vsnprintf(buf, sizeof(buf), format, args);

    DBG_DIRECT("[GUI MODULE]%s", buf);

    va_end(args);
}

void *lvgl_task_handle;

extern void disp_enable_update(void);
extern void disp_disable_update(void);

#if LV_USE_DEMO_BENCHMARK
static void on_benchmark_finished(void)
{
    disp_enable_update();
}
#endif


/* 1 ms tick from os, better than lv_tick_inc() in a 10 ms soft timer */
static uint32_t port_tick_get(void)
{
    return (uint32_t)os_sys_time_get();
}

static void lvgl_demo_run(void *p)
{
    lv_init();
    lv_tick_set_cb(port_tick_get);
    lv_log_register_print_cb((lv_log_print_g_cb_t)port_log);
    lv_port_disp_init();
    lv_port_indev_init();
#if LV_USE_DEMO_BENCHMARK
    LV_LOG("Running LVGL Benchmark... \n");
    lv_demo_benchmark();
#elif LV_USE_DEMO_WIDGETS
    lv_demo_widgets();
#endif
    // lv_obj_t *screen = lv_scr_act();
    // lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);

    // lv_obj_t* rect = lv_obj_create(screen);
    // lv_obj_set_size(rect, 320, 20);
    // lv_obj_set_pos(rect, 10, 10);
    // lv_obj_set_style_radius(rect, 0, 0);
    // lv_obj_set_style_bg_color(rect, lv_color_hex(0x0000ff), 0);
    // lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
    // lv_obj_set_style_border_width(rect, 0, 0);

    while (1)
    {
        /* sleep until the next lv timer is due, so other tasks and dlps can run */
        uint32_t time_till_next = lv_timer_handler();
        if (time_till_next == LV_NO_TIMER_READY)
        {
            time_till_next = LV_DEF_REFR_PERIOD;
        }
        os_delay(LV_CLAMP(1, time_till_next, LV_DEF_REFR_PERIOD));
    }
}

void rt_lvgl_demo_init(void)
{
    /* littleGL demo gui thread */
    os_task_create(&lvgl_task_handle, "lvgl", lvgl_demo_run, 0, APP_TASK_STACK_SIZE,
                   APP_TASK_PRIORITY);
}

