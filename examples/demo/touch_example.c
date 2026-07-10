/* ================================================================
 * Touch 使用示例
 *
 * 应用只 open /dev/touch0，不关心底下是哪颗芯片——板级
 * posix_port_touch.c 已在启动阶段完成绑定。
 *
 * posix_read(/dev/touch0) 语义（面向 posix touch 抽象层，与具体芯片无关）：
 *   ret > 0，point_count > 0             → 有触摸帧，data.points[0] 有效
 *   ret > 0，point_count = 0             → 松手帧（端口层可能靠软定时器合成）
 *   ret == POSIX_ERR_TIMEOUT             → 一段时间内没等到 INT，也视作稳态松手
 *   ret < 0 且不是 TIMEOUT               → I²C / 框架错误
 *
 * 消费模型（对齐 Linux input subsystem）：
 *   Linux 上触摸屏驱动通用做法是 request_threaded_irq，INT 唤醒 kthread
 *   走 I²C，把数据 input_report 到 /dev/input/eventN；应用只 read/poll
 *   一条 fd，语义就是"中断驱动的自动读取"。这里 posix_read(/dev/touch0)
 *   完全对齐——阻塞在 INT 上，来了就返回一帧。想持续接触摸事件，起一个
 *   线程死循环 posix_read 即可（见下面的 monitor shell 命令）。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_touch.h"

void example_touch(void)
{
    /* === 1. 打开 === */
    posix_fd_t tp = posix_open("/dev/touch0");
    if (!tp) { return; }

    /* === 2. 配置（面板分辨率 / swap_xy；I²C 从机地址不在上层配置里） === */
    posix_touch_config_t cfg =
    {
        .width    = 360,     /* 按当前面板改 */
        .height   = 360,
        .swap_xy  = 0,
    };
    posix_ioctl(tp, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 读触摸数据（阻塞等 INT，超时行为随芯片而异） === */
    posix_touch_data_t tdata;
    int ret = posix_read(tp, &tdata, sizeof(tdata));

    if (ret >= 0 && tdata.point_count > 0)
    {
        /* tdata.points[0].x, .y, .status */
    }

    /* === 4. 关闭 === */
    posix_close(tp);
}



#ifdef CONFIG_SHELL
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "posix_port.h"
LOG_MODULE_REGISTER(touch_demo, LOG_LEVEL_INF);

/* ================================================================
 * 唯一的 shell 命令：posix_touch monitor
 *
 * 敲一次：lazy 打开 /dev/touch0，起一个后台线程死循环 posix_read，
 * 触摸事件由 printk 直接打出。整个过程无轮询，中断由端口层 ISR
 * 消化，形态对齐 Linux 上的 evtest。
 *
 * 不提供 off——这本来就是 demo，需要停就复位板子。真要产品化，把
 * 消费方换成正经的 input 事件队列，不要在这里叠停/开状态机。
 * ================================================================ */
#define TOUCH_MON_STACK   1024
#define TOUCH_MON_PRIO    10

static K_THREAD_STACK_DEFINE(s_mon_stack, TOUCH_MON_STACK);
static struct k_thread s_mon_thread;
static bool            s_mon_started;

static void touch_monitor_task(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;

    /* 一次性 lazy init + open，坏了就退线程；不做重试。 */
    posix_port_init_all();

    posix_fd_t fd = posix_open("/dev/touch0");
    if (fd == POSIX_FD_NULL)
    {
        printk("[touch mon] open /dev/touch0 failed, monitor thread exits\n");
        return;
    }

    posix_touch_config_t cfg = { .width = 360, .height = 360, .swap_xy = 0 };
    posix_ioctl(fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    /* 只在 press↔release 沿变化时打印，稳态释放不刷屏。 */
    bool prev_pressed = false;

    for (;;)
    {
        posix_touch_data_t data;
        int ret = posix_read(fd, &data, sizeof(data));

        if (ret == POSIX_ERR_TIMEOUT)
        {
            /* 一段时间内没等到 INT；视作稳态松手，不打印。 */
            continue;
        }
        if (ret < 0)
        {
            printk("[touch mon] read failed: %d\n", ret);
            k_msleep(20);           /* 避免抱死循环 */
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
            /* 只在从按压回到释放时打一次，避免稳态 release 循环刷屏。 */
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

