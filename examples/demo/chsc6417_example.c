/* ================================================================
 * CHSC6417 Touch 使用示例
 *
 * 与 cst816d_example.c 并列存在；两份 demo 都会被 CMake glob 编入，
 * 分别注册 shell 命令 posix_chsc6417 / posix_cst816d。
 *
 * 功能：打开 /dev/chsc6417 -> 配置 -> 读取触摸点 -> 关闭
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_touch.h"

void example_chsc6417(void)
{
    /* === 1. 打开 === */
    posix_fd_t tp = posix_open("/dev/chsc6417");
    if (!tp) { return; }

    /* === 2. 配置（i2c_addr/分辨率/swap_xy） === */
    posix_touch_config_t cfg =
    {
        .i2c_addr = 0x2E,    /* CHSC6417 地址 */
        .width    = 410,
        .height   = 502,
        .swap_xy  = 0,
    };
    posix_ioctl(tp, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 读触摸数据（中断模式下 posix_read 会阻塞等 INT，
     *        100ms 内无 INT 返回 POSIX_ERR_TIMEOUT） === */
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
LOG_MODULE_REGISTER(chsc6417_demo, LOG_LEVEL_INF);

static bool       s_posix_chsc6417_inited = false;
static posix_fd_t s_chsc6417_fd = POSIX_FD_NULL;

static int cmd_chsc6417_open(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_posix_chsc6417_inited) { posix_port_init_all(); s_posix_chsc6417_inited = true; }

    if (s_chsc6417_fd != POSIX_FD_NULL)
    {
        shell_print(sh, "already open");
        return 0;
    }

    s_chsc6417_fd = posix_open("/dev/chsc6417");
    if (s_chsc6417_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/chsc6417 failed");
        return -1;
    }

    posix_touch_config_t cfg = { .i2c_addr = 0x2E, .width = 410, .height = 502, .swap_xy = 0 };
    posix_ioctl(s_chsc6417_fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    shell_print(sh, "chsc6417 opened, INT armed");
    return 0;
}

static int cmd_chsc6417_read(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_chsc6417_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "not open, run 'posix_chsc6417 open' first");
        return -1;
    }

    posix_touch_data_t data;
    int ret = posix_read(s_chsc6417_fd, &data, sizeof(data));
    if (ret == POSIX_ERR_TIMEOUT)
    {
        shell_print(sh, "no touch within 100ms");
    }
    else if (ret < 0)
    {
        shell_error(sh, "read failed: %d", ret);
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

static int cmd_chsc6417_close(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_chsc6417_fd != POSIX_FD_NULL)
    {
        posix_close(s_chsc6417_fd);
        s_chsc6417_fd = POSIX_FD_NULL;
    }
    shell_print(sh, "chsc6417 closed");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(chsc6417_cmds,
                               SHELL_CMD(open,  NULL, "Open /dev/chsc6417 and arm INT", cmd_chsc6417_open),
                               SHELL_CMD(read,  NULL, "Read one touch point (blocks 100ms)", cmd_chsc6417_read),
                               SHELL_CMD(close, NULL, "Close /dev/chsc6417", cmd_chsc6417_close),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_chsc6417, &chsc6417_cmds, "posix_chsc6417 [open|read|close]", NULL);
#endif /* CONFIG_SHELL */
