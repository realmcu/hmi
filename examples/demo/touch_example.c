/* ================================================================
 * Touch usage example
 *
 * The app only opens /dev/touch0 and does not care which chip is underneath -- the board-level
 * posix_port_touch.c already performed the binding at startup.
 *
 * posix_read(/dev/touch0) semantics (facing the posix touch abstraction, chip-independent):
 *   ret > 0, point_count > 0             → touch frame, data.points[0] valid
 *   ret > 0, point_count = 0             → release frame (port may synthesize it with a soft timer)
 *   ret == POSIX_ERR_TIMEOUT             → no INT within the window; also treated as steady release
 *   ret < 0 and not TIMEOUT              → I²C / framework error
 *
 * Consumption model (aligned with the Linux input subsystem):
 *   The common Linux touchscreen driver approach is request_threaded_irq, where INT wakes a kthread
 *   that does I²C and input_report to /dev/input/eventN; the app only read/poll
 *   one fd, whose semantics are "interrupt-driven automatic reads". Here posix_read(/dev/touch0)
 *   aligns fully -- it blocks on INT and returns a frame when one arrives. To consume touch events continuously,
 *   start a thread looping on posix_read (see the monitor shell command below).
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_touch.h"

void example_touch(void)
{
    /* === 1. Open === */
    posix_fd_t tp = posix_open("/dev/touch0");
    if (!tp) { return; }

    /* === 2. Configure (panel resolution / swap_xy; I²C slave address is not in the upper-layer config) === */
    posix_touch_config_t cfg =
    {
        .width    = 360,     /* change for the current panel */
        .height   = 360,
        .swap_xy  = 0,
    };
    posix_ioctl(tp, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    /* === 3. Read touch data (blocks waiting for INT; timeout behavior varies by chip) === */
    posix_touch_data_t tdata;
    int ret = posix_read(tp, &tdata, sizeof(tdata));

    if (ret >= 0 && tdata.point_count > 0)
    {
        /* tdata.points[0].x, .y, .status */
    }

    /* === 4. Close === */
    posix_close(tp);
}



#ifdef CONFIG_SHELL
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "posix_port.h"
LOG_MODULE_REGISTER(touch_demo, LOG_LEVEL_INF);

/* ================================================================
 * The only shell command: posix_touch monitor
 *
 * Run once: lazily open /dev/touch0, start a background thread looping on posix_read,
 * and print touch events directly with printk. There is no polling; interrupts are
 * absorbed by the port ISR, shaped like evtest on Linux.
 *
 * No off switch -- this is a demo; reset the board to stop. For real products, replace the
 * consumer with a proper input event queue; do not pile stop/start state machines on here.
 * ================================================================ */
#define TOUCH_MON_STACK   1024
#define TOUCH_MON_PRIO    10

static K_THREAD_STACK_DEFINE(s_mon_stack, TOUCH_MON_STACK);
static struct k_thread s_mon_thread;
static bool            s_mon_started;

static void touch_monitor_task(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;

    /* One-shot lazy init + open; exit the thread on failure, no retry. */
    posix_port_init_all();

    posix_fd_t fd = posix_open("/dev/touch0");
    if (fd == POSIX_FD_NULL)
    {
        printk("[touch mon] open /dev/touch0 failed, monitor thread exits\n");
        return;
    }

    posix_touch_config_t cfg = { .width = 360, .height = 360, .swap_xy = 0 };
    posix_ioctl(fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    /* Print only on press↔release edge changes; steady release does not flood the console. */
    bool prev_pressed = false;

    for (;;)
    {
        posix_touch_data_t data;
        int ret = posix_read(fd, &data, sizeof(data));

        if (ret == POSIX_ERR_TIMEOUT)
        {
            /* no INT within the window; treated as steady release, do not print. */
            continue;
        }
        if (ret < 0)
        {
            printk("[touch mon] read failed: %d\n", ret);
            k_msleep(20);           /* avoid a tight busy loop */
            continue;
        }

        bool pressed = (data.point_count > 0 &&
                        data.points[0].status == POSIX_TOUCH_PRESS);

        if (pressed)
        {
            printk("[touch mon] x=%u y=%u press\n",
                   data.points[0].x, data.points[0].y);
        }
        else if (prev_pressed)
        {
            /* Print once when going from pressed back to released, avoiding steady-release flood. */
            printk("[touch mon] release\n");
        }
        prev_pressed = pressed;
    }
}

static int cmd_touch_monitor(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_mon_started)
    {
        shell_print(sh, "monitor already running");
        return 0;
    }
    s_mon_started = true;

    k_tid_t tid = k_thread_create(&s_mon_thread, s_mon_stack,
                                  K_THREAD_STACK_SIZEOF(s_mon_stack),
                                  touch_monitor_task, NULL, NULL, NULL,
                                  K_PRIO_PREEMPT(TOUCH_MON_PRIO), 0, K_NO_WAIT);
    k_thread_name_set(tid, "touch_mon");

    shell_print(sh, "monitor started; touch events go to console via [touch mon]");
    return 0;
}

SHELL_CMD_REGISTER(posix_touch, NULL,
                   "posix_touch : start touch event monitor (one-shot)",
                   cmd_touch_monitor);
#endif /* CONFIG_SHELL */

