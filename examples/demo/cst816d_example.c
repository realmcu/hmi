/* ================================================================
 * CST816D Touch 使用示例
 *
 * 与 chsc6417_example.c 并列存在；两份 demo 都会被 CMake glob 编入，
 * 分别注册 shell 命令 posix_chsc6417 / posix_cst816d。
 *
 * 功能：打开 /dev/cst816d -> 配置 -> 读取触摸点 -> 关闭
 *
 * 语义说明（对比 CHSC6417 版）：
 *   CST816D 一次按压只产生 1 次 INT，port 层用 posix_sem_take(release_ms)
 *   将“无新中断”表达为“释放”。因此 posix_read 不会返回 POSIX_ERR_TIMEOUT，
 *   超时时返回一个 point_count=0 / status=RELEASE 的正常帧。
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_touch.h"

void example_cst816d(void)
{
    /* === 1. 打开 === */
    posix_fd_t tp = posix_open("/dev/cst816d");
    if (!tp) { return; }

    /* === 2. 配置（i2c_addr/分辨率/swap_xy） === */
    posix_touch_config_t cfg =
    {
        .i2c_addr = 0x15,    /* CST816D 上游 dts 通用地址 */
        .width    = 360,     /* eBadge ST77916 面板 360x360 */
        .height   = 360,
        .swap_xy  = 0,
    };
    posix_ioctl(tp, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 读触摸数据（中断模式下 posix_read 会阻塞等 INT，
     *        release_ms 内无 INT 则返回 point_count=0 的释放帧） === */
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
#include "posix_port.h"
LOG_MODULE_REGISTER(cst816d_demo, LOG_LEVEL_INF);

static bool       s_posix_cst816d_inited = false;
static posix_fd_t s_cst816d_fd = POSIX_FD_NULL;

static int cmd_cst816d_open(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_posix_cst816d_inited) { posix_port_init_all(); s_posix_cst816d_inited = true; }

    if (s_cst816d_fd != POSIX_FD_NULL)
    {
        shell_print(sh, "already open");
        return 0;
    }

    s_cst816d_fd = posix_open("/dev/cst816d");
    if (s_cst816d_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/cst816d failed");
        return -1;
    }

    posix_touch_config_t cfg = { .i2c_addr = 0x15, .width = 360, .height = 360, .swap_xy = 0 };
    posix_ioctl(s_cst816d_fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    shell_print(sh, "cst816d opened, INT armed");
    return 0;
}

static int cmd_cst816d_read(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_cst816d_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "not open, run 'posix_cst816d open' first");
        return -1;
    }

    posix_touch_data_t data;
    int ret = posix_read(s_cst816d_fd, &data, sizeof(data));
    if (ret < 0)
    {
        shell_error(sh, "read failed: %d", ret);
    }
    else if (data.point_count == 0)
    {
        /* release_ms 内无新 INT 或本次事件本身就是抬手 */
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

static int cmd_cst816d_close(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_cst816d_fd != POSIX_FD_NULL)
    {
        posix_close(s_cst816d_fd);
        s_cst816d_fd = POSIX_FD_NULL;
    }
    shell_print(sh, "cst816d closed");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(cst816d_cmds,
                               SHELL_CMD(open,  NULL, "Open /dev/cst816d and arm INT", cmd_cst816d_open),
                               SHELL_CMD(read,  NULL, "Read one touch point (blocks up to release_ms)", cmd_cst816d_read),
                               SHELL_CMD(close, NULL, "Close /dev/cst816d", cmd_cst816d_close),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_cst816d, &cst816d_cmds, "posix_cst816d [open|read|close]", NULL);
#endif /* CONFIG_SHELL */
