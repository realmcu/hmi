/* ================================================================
 * Touch 使用示例（合并原 cst816d_example.c / chsc6417_example.c）
 *
 * 有了 /dev/touch0 之后，应用层不再关心底下是哪颗芯片。本 demo 主打
 * 三个用法：
 *
 *   1. 直接操作 /dev/touch0（板级 posix_port_touch.c 已在启动阶段把
 *      当前板对应芯片绑到这里；见 examples/port/common/posix_port_touch.c）
 *
 *   2. 传路径参数直接开某颗芯片，用于调试/回归——两颗芯片自身路径
 *      /dev/cst816d、/dev/chsc6417 始终可见
 *
 *   3. 演示 posix_touch_bind()：运行时把不同芯片挂到自定义 alias
 *
 * 语义提示：
 *   posix_read 无触摸时两种返回都可能：
 *     - CST816D  : 返回 point_count=0 / status=RELEASE 的正常帧
 *     - CHSC6417 : 返回 POSIX_ERR_TIMEOUT（100ms 无 INT）
 *   本 demo 的 shell 分支同时处理两种。
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

/* ================================================================
 * Boot 自测：main() 里直接调用，不用敲 shell 命令
 *
 * 起一个独立线程做单次读取：
 *   posix_port_init_all → open /dev/touch0 → 阻塞等一次 INT
 *   → 打印一次结果 → close → 线程退出
 *
 * 想再读就再敲 shell 命令 posix_touch open/read/close。
 * ================================================================ */
#include <zephyr/kernel.h>

#define TOUCH_SELFTEST_STACK   1024
#define TOUCH_SELFTEST_PRIO    10

static K_THREAD_STACK_DEFINE(s_selftest_stack, TOUCH_SELFTEST_STACK);
static struct k_thread       s_selftest_thread;

static void touch_selftest_task(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;

    /* 保证 posix 设备表已注册（其实 posix_init.c 用链接段自动跑过了；
     * 显式再调一次是幂等的，防止启动时序有变时静默失败）。 */
    posix_port_init_all();

    posix_fd_t tp = posix_open("/dev/touch0");
    if (tp == POSIX_FD_NULL)
    {
        printk("[touch selftest] open /dev/touch0 failed\n");
        return;
    }

    posix_touch_config_t cfg =
    {
        .width = 360, .height = 360, .swap_xy = 0,
    };
    posix_ioctl(tp, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    printk("[touch selftest] /dev/touch0 opened, INT armed. Touch the screen.\n");

    /* 单次读取：阻塞直到 INT 到达（或芯片超时视为 release） */
    posix_touch_data_t data;
    int ret = posix_read(tp, &data, sizeof(data));

    if (ret == POSIX_ERR_TIMEOUT)
    {
        printk("[touch selftest] no touch (timeout)\n");
    }
    else if (ret < 0)
    {
        printk("[touch selftest] read err %d\n", ret);
    }
    else if (data.point_count == 0)
    {
        printk("[touch selftest] release (no touch in release window)\n");
    }
    else
    {
        printk("[touch selftest] x=%u y=%u %s\n",
               data.points[0].x, data.points[0].y,
               data.points[0].status == POSIX_TOUCH_PRESS ? "press" : "release");
    }

    posix_close(tp);
    printk("[touch selftest] done, /dev/touch0 closed\n");
}

void touch_selftest_start(void)
{
    k_thread_create(&s_selftest_thread, s_selftest_stack,
                    K_THREAD_STACK_SIZEOF(s_selftest_stack),
                    touch_selftest_task, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(TOUCH_SELFTEST_PRIO), 0, K_NO_WAIT);
    k_thread_name_set(&s_selftest_thread, "touch_st");
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "posix_port.h"
LOG_MODULE_REGISTER(touch_demo, LOG_LEVEL_INF);

#define TOUCH_DEFAULT_PATH  "/dev/touch0"

static bool       s_posix_touch_inited = false;
static posix_fd_t s_touch_fd = POSIX_FD_NULL;
static char       s_touch_opened_path[32];

/* posix_touch open [/dev/touch0 | /dev/cst816d | /dev/chsc6417] */
static int cmd_touch_open(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_posix_touch_inited) { posix_port_init_all(); s_posix_touch_inited = true; }

    if (s_touch_fd != POSIX_FD_NULL)
    {
        shell_print(sh, "already open: %s", s_touch_opened_path);
        return 0;
    }

    const char *path = (argc >= 2) ? argv[1] : TOUCH_DEFAULT_PATH;
    s_touch_fd = posix_open(path);
    if (s_touch_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open %s failed", path);
        return -1;
    }
    strncpy(s_touch_opened_path, path, sizeof(s_touch_opened_path) - 1);
    s_touch_opened_path[sizeof(s_touch_opened_path) - 1] = '\0';

    /* 面板尺寸留一份默认；用户可后续用 ioctl 覆盖 */
    posix_touch_config_t cfg = { .width = 360, .height = 360, .swap_xy = 0 };
    posix_ioctl(s_touch_fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    shell_print(sh, "%s opened, INT armed. Run 'posix_touch read' to read one sample.", path);
    return 0;
}

/* posix_touch read
 *
 * 单次读取：阻塞直到 INT 到来（或芯片的 release 超时），打印一次结果就返回。
 * 想连续观察就重复敲 read；close 释放 fd。
 */
static int cmd_touch_read(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_touch_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "not open, run 'posix_touch open' first");
        return -1;
    }

    posix_touch_data_t data;
    int ret = posix_read(s_touch_fd, &data, sizeof(data));

    if (ret == POSIX_ERR_TIMEOUT)
    {
        /* CHSC6417 语义：100ms 内无 INT */
        shell_print(sh, "no touch (timeout)");
    }
    else if (ret < 0)
    {
        shell_error(sh, "read failed: %d", ret);
    }
    else if (data.point_count == 0)
    {
        /* CST816D 语义：release_ms 内无新 INT，返回释放帧 */
        shell_print(sh, "release (no touch in release window)");
    }
    else
    {
        shell_print(sh, "points=%d x=%d y=%d status=%s",
                    data.point_count,
                    data.points[0].x, data.points[0].y,
                    data.points[0].status == POSIX_TOUCH_PRESS ? "press" : "release");
    }
    return 0;
}

static int cmd_touch_close(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_touch_fd != POSIX_FD_NULL)
    {
        posix_close(s_touch_fd);
        s_touch_fd = POSIX_FD_NULL;
        s_touch_opened_path[0] = '\0';
    }
    shell_print(sh, "touch closed");
    return 0;
}


SHELL_STATIC_SUBCMD_SET_CREATE(touch_cmds,
                               SHELL_CMD_ARG(open,   NULL,
                                             "Open touch device (default /dev/touch0). "
                                             "Optional path: /dev/cst816d | /dev/chsc6417 | <alias>",
                                             cmd_touch_open, 1, 1),
                               SHELL_CMD(read,   NULL,
                                         "Read one touch sample (blocks until INT or timeout)",
                                         cmd_touch_read),
                               SHELL_CMD(close,  NULL,
                                         "Close current touch fd",
                                         cmd_touch_close),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_touch, &touch_cmds,
                   "posix_touch [open [path]|read|close|bind|unbind]", NULL);
#endif /* CONFIG_SHELL */
