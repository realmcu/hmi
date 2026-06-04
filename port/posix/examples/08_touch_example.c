/* ================================================================
 * Touch 使用示例
 *
 * 功能：打开 Touch0 → 配置 → 校准 → 读取触摸点 → 关闭
 *
 * 编译要求：需要 posix.h + posix_ioctl_touch.h
 * ================================================================ */
// Zephyr Shell test: uart:~$ posix_touch read

#include "posix.h"
#include "ioctls/posix_ioctl_touch.h"

void example_touch(void)
{
    /* === 1. 打开 === */
    posix_fd_t tp = posix_open("/dev/touch0");
    if (!tp) { return; }

    /* === 2. 配置 === */
    posix_touch_config_t cfg =
    {
        .i2c_addr = 0x38,
        .width = 320, .height = 480,
        .swap_xy = 0,
    };
    posix_ioctl(tp, POSIX_TOUCH_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 校准 === */
    posix_ioctl(tp, POSIX_TOUCH_IOCTL_CALIBRATE, NULL);

    /* === 4. 读触摸数据（轮询方式） === */
    posix_touch_data_t tdata;
    posix_read(tp, &tdata, sizeof(tdata));

    if (tdata.point_count > 0)
    {
        for (int i = 0; i < tdata.point_count; i++)
        {
            /* tdata.points[i].x, .y, .pressure, .status */
        }
    }

    /* === 5. 关闭 === */
    posix_close(tp);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_posix_touch_inited = false;

static int cmd_touch_read(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_posix_touch_inited)
    {
        posix_port_init_all();
        s_posix_touch_inited = true;
    }
    posix_fd_t fd = posix_open("/dev/touch0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/touch0 failed"); return -1; }
    posix_touch_data_t data;
    int ret = posix_read(fd, &data, sizeof(data));
    if (ret >= 0)
    {
        shell_print(sh, "touch: x=%d y=%d pressed=%d",
                    data.points[0].x, data.points[0].y,
                    (data.point_count > 0 && data.points[0].status == POSIX_TOUCH_PRESS) ? 1 : 0);
    }
    else
    {
        shell_error(sh, "read failed: %d", ret);
    }
    posix_close(fd);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(touch_cmds,
                               SHELL_CMD(read, NULL, "Read one touch point", cmd_touch_read),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_touch, &touch_cmds, "POSIX touch driver test", NULL);
#endif /* CONFIG_SHELL */
