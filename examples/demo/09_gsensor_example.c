// Zephyr Shell test: uart:~$ posix_gsensor
/* ================================================================
 * G-sensor 使用示例
 *
 * 功能：打开 G-sensor → 配置量程 → 读三轴数据 → 读温度 → 关闭
 *
 * 编译要求：需要 posix.h + posix_ioctl_gsensor.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_gsensor.h"

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
static int cmd_gsensor_read(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "gsensor: not yet implemented (stub driver)");
    shell_print(sh, "TODO: implement with RTK I2C API when gsensor driver is available");
    return 0;
}
SHELL_CMD_REGISTER(posix_gsensor, NULL, "POSIX gsensor test (stub)", cmd_gsensor_read);
#endif /* CONFIG_SHELL */

void example_gsensor(void)
{
    /* === 1. 打开 === */
    posix_fd_t gs = posix_open("/dev/gsensor0");
    if (!gs) { return; }

    /* === 2. 配置量程 ±2G，100Hz === */
    posix_gsensor_config_t cfg =
    {
        .range     = POSIX_GSENSOR_RANGE_2G,
        .odr_hz    = 100,
        .low_power = 0,
    };
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 读三轴加速度 === */
    posix_gsensor_axis_t accel;
    posix_read(gs, &accel, sizeof(accel));
    /* accel.x/y/z 单位 mg（千分之一 g） */

    /* === 4. 读芯片温度 === */
    int temp;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_READ_TEMP, &temp);
    /* temp 单位 0.1°C，如 250 = 25.0°C */

    /* === 5. 自检 === */
    int ret = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SELF_TEST, NULL);
    /* ret == 0 表示正常 */

    /* === 6. 关闭 === */
    posix_close(gs);
    (void)ret;
}
