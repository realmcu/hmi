/* ================================================================
 * Touch 使用示例
 *
 * 功能：打开 Touch0 -> 配置 -> 读取触摸点 -> 关闭
 *
 * 编译要求：需要 posix.h + posix_ioctl_touch.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_touch.h"

void example_touch(void)
{
    /* === 1. 打开 === */
    posix_fd_t tp = posix_open("/dev/touch0");
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

    /* === 3. 读触摸数据（中断模式下 posix_read 会阻塞等 INT） === */
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
LOG_MODULE_REGISTER(touch_demo, LOG_LEVEL_INF);

static bool       s_posix_touch_inited = false;
static posix_fd_t s_touch_fd = POSIX_FD_NULL;

static int cmd_touch_open(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_posix_touch_inited) { posix_port_init_all(); s_posix_touch_inited = true; }

    if (s_touch_fd != POSIX_FD_NULL)
    {
        shell_print(sh, "already open");
        return 0;
    }

    s_touch_fd = posix_open("/dev/touch0");
    if (s_touch_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/touch0 failed");
        return -1;
    }

    posix_touch_config_t cfg = { .i2c_addr = 0x2E, .width = 410, .height = 502, .swap_xy = 0 };
    posix_ioctl(s_touch_fd, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    shell_print(sh, "touch0 opened, INT armed");
    return 0;
}

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

static int cmd_touch_close(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_touch_fd != POSIX_FD_NULL)
    {
        posix_close(s_touch_fd);
        s_touch_fd = POSIX_FD_NULL;
    }
    shell_print(sh, "touch0 closed");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(touch_cmds,
                               SHELL_CMD(open,  NULL, "Open touch0 and arm INT", cmd_touch_open),
                               SHELL_CMD(read,  NULL, "Read one touch point (blocks 100ms)", cmd_touch_read),
                               SHELL_CMD(close, NULL, "Close touch0", cmd_touch_close),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_touch, &touch_cmds, "posix_touch [open|read|close]", NULL);
#endif /* CONFIG_SHELL */
