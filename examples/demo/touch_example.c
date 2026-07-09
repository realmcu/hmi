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
#include "ioctls/posix_ioctl_touch.h"

void example_touch(void)
{
    /* === 1. 打开 === */
    posix_fd_t tp = posix_open("/dev/touch0");
    if (!tp) { return; }

    /* === 2. 配置（面板分辨率 / swap_xy；i2c_addr 由 port 层内置） === */
    posix_touch_config_t cfg =
    {
        .i2c_addr = 0,       /* 0 = 保留 port 内置地址 */
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
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "posix_port.h"
LOG_MODULE_REGISTER(touch_demo, LOG_LEVEL_INF);

#define TOUCH_DEFAULT_PATH  "/dev/touch0"

static bool       s_posix_touch_inited = false;
static posix_fd_t s_touch_fd = POSIX_FD_NULL;
static char       s_touch_opened_path[32];

/* ---------- 持续读线程 ---------- *
 * open 后由 read 子命令启动，close 停。posix_read 会阻塞在芯片 INT
 * 信号量上（CST816D release_ms=30ms 超时/CHSC6417 100ms 超时），因此
 * 线程能感知到 s_reader_running 变化的最坏延迟就是这个超时窗口。
 */
static K_THREAD_STACK_DEFINE(s_reader_stack, 1024);
static struct k_thread     s_reader_thread;
static volatile bool       s_reader_running = false;
static struct k_sem        s_reader_done;   /* 线程退出后 give，close 端 take */
static bool                s_reader_done_inited = false;

static void touch_reader_task(void *p1, void *p2, void *p3)
{
    const struct shell *sh = (const struct shell *)p1;
    (void)p2; (void)p3;

    while (s_reader_running)
    {
        posix_touch_data_t data;
        int ret = posix_read(s_touch_fd, &data, sizeof(data));

        if (!s_reader_running) { break; }   /* close 期间被叫醒，直接退 */

        if (ret == POSIX_ERR_TIMEOUT)
        {
            /* CHSC6417 语义：100ms 内无 INT —— 不打印，避免刷屏 */
            continue;
        }
        if (ret < 0)
        {
            shell_error(sh, "read failed: %d", ret);
            continue;
        }
        if (data.point_count == 0)
        {
            /* CST816D 语义：release_ms 内无新 INT，返回释放帧 —— 不打印 */
            continue;
        }

        /* 有触摸事件才打印 */
        shell_print(sh, "points=%d x=%d y=%d status=%s",
                    data.point_count,
                    data.points[0].x, data.points[0].y,
                    data.points[0].status == POSIX_TOUCH_PRESS ? "press" : "release");
    }

    k_sem_give(&s_reader_done);
}

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
    posix_touch_config_t cfg = { .i2c_addr = 0, .width = 360, .height = 360, .swap_xy = 0 };
    posix_ioctl(s_touch_fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    shell_print(sh, "%s opened, INT armed. Run 'posix_touch read' to start streaming.",
                path);
    return 0;
}

/* posix_touch read
 *
 * 启动后台线程持续读；每次触发中断（手指触屏）就打印一次坐标。
 * 用 'posix_touch close' 停止并释放 fd。
 */
static int cmd_touch_read(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_touch_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "not open, run 'posix_touch open' first");
        return -1;
    }
    if (s_reader_running)
    {
        shell_print(sh, "already reading; run 'posix_touch close' to stop");
        return 0;
    }

    if (!s_reader_done_inited)
    {
        k_sem_init(&s_reader_done, 0, 1);
        s_reader_done_inited = true;
    }
    else
    {
        k_sem_reset(&s_reader_done);
    }

    s_reader_running = true;
    k_thread_create(&s_reader_thread, s_reader_stack,
                    K_THREAD_STACK_SIZEOF(s_reader_stack),
                    touch_reader_task, (void *)sh, NULL, NULL,
                    K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
    k_thread_name_set(&s_reader_thread, "touch_rd");

    shell_print(sh, "streaming touch events (INT-driven). "
                "Touch the screen; run 'posix_touch close' to stop.");
    return 0;
}

static int cmd_touch_close(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;

    /* 1) 先让线程停 —— 等它自己走完最后一轮 read（最坏 100ms） */
    if (s_reader_running)
    {
        s_reader_running = false;
        (void)k_sem_take(&s_reader_done, K_MSEC(500));
    }

    /* 2) 再关 fd */
    if (s_touch_fd != POSIX_FD_NULL)
    {
        posix_close(s_touch_fd);
        s_touch_fd = POSIX_FD_NULL;
        s_touch_opened_path[0] = '\0';
    }
    shell_print(sh, "touch closed");
    return 0;
}

/* posix_touch bind <alias> <chip_path>
 *   例：posix_touch bind /dev/my_touch /dev/chsc6417
 * 演示 posix_touch_bind() —— 板级 posix_port_touch.c 已经在启动阶段
 * 绑好 /dev/touch0，这里给出运行时再绑一个别名的例子。 */
static int cmd_touch_bind(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3)
    {
        shell_error(sh, "usage: posix_touch bind <alias> <chip_path>");
        return -1;
    }
    if (!s_posix_touch_inited) { posix_port_init_all(); s_posix_touch_inited = true; }

    int ret = posix_touch_bind(argv[1], argv[2]);
    if (ret != POSIX_OK)
    {
        shell_error(sh, "bind %s -> %s failed: %d", argv[1], argv[2], ret);
        return -1;
    }
    shell_print(sh, "bound %s -> %s", argv[1], argv[2]);
    return 0;
}

static int cmd_touch_unbind(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2)
    {
        shell_error(sh, "usage: posix_touch unbind <alias>");
        return -1;
    }
    int ret = posix_touch_unbind(argv[1]);
    if (ret != POSIX_OK)
    {
        shell_error(sh, "unbind %s failed: %d (busy? not bound?)", argv[1], ret);
        return -1;
    }
    shell_print(sh, "unbound %s", argv[1]);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(touch_cmds,
                               SHELL_CMD_ARG(open,   NULL,
                                             "Open touch device (default /dev/touch0). "
                                             "Optional path: /dev/cst816d | /dev/chsc6417 | <alias>",
                                             cmd_touch_open, 1, 1),
                               SHELL_CMD(read,   NULL,
                                         "Start streaming touch events; prints on every INT until 'close'",
                                         cmd_touch_read),
                               SHELL_CMD(close,  NULL,
                                         "Stop streaming and close current touch fd",
                                         cmd_touch_close),
                               SHELL_CMD_ARG(bind,   NULL,
                                             "posix_touch bind <alias> <chip_path>",
                                             cmd_touch_bind, 3, 0),
                               SHELL_CMD_ARG(unbind, NULL,
                                             "posix_touch unbind <alias>",
                                             cmd_touch_unbind, 2, 0),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_touch, &touch_cmds,
                   "posix_touch [open [path]|read|close|bind|unbind]", NULL);
#endif /* CONFIG_SHELL */
