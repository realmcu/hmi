/* ================================================================
 * PWM 使用示例
 *
 * 功能：打开 PWM0 → 配置 50Hz 舵机控制 → 动态调占空比 → 关闭
 *
 * PWM 没有流式数据，read/write 返回 NOSUPP，
 * 全部操作通过 ioctl 完成。
 *
 * 编译要求：需要 posix.h + posix_ioctl_pwm.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_pwm.h"

void example_pwm(void)
{
    /* === 1. 打开设备 === */
    posix_fd_t pwm = posix_open("/dev/pwm0");
    if (pwm == POSIX_FD_NULL)
    {
        return;
    }

    /* === 2. 配置：50Hz, 7.5% 占空比 === */
    /* 舵机：50Hz 周期 20ms
     *   0.5ms 脉宽  →  0°  (2.5%)
     *   1.5ms 脉宽  →  90° (7.5%)
     *   2.5ms 脉宽  →  180°(12.5%) */
    posix_pwm_config_t cfg =
    {
        .channel    = 0,
        .freq_hz    = 50,           /* 20ms 周期 */
        .duty_cycle = 0.075f,       /* 1.5ms 脉宽 = 中位 */
        .polarity   = POSIX_PWM_POLARITY_NORMAL,
    };
    posix_ioctl(pwm, POSIX_PWM_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 启动输出 === */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_START, &(int) {0}); /* channel 0 */

    /* === 4. 动态调脉宽（微秒） === */
    /* 转到 0° */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_SET_PULSE,
    &(posix_pwm_config_t) { .channel = 0, .pulse_us = 500 });
    /* 延时... */

    /* 转到 180° */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_SET_DUTY,
    &(posix_pwm_config_t) { .channel = 0, .duty_cycle = 0.125f });

    /* === 5. 停止输出 === */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_STOP, &(int) {0});

    /* === 6. 关闭 === */
    posix_close(pwm);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_pwm_inited = false;

static int cmd_pwm_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_pwm_inited) { posix_port_init_all(); s_pwm_inited = true; }
    posix_fd_t fd = posix_open("/dev/pwm0");
    if (fd == POSIX_FD_NULL) { shell_print(sh, "pwm0 not registered — no port driver yet"); return 0; }
    shell_print(sh, "open OK");
    posix_close(fd);
    return 0;
}
SHELL_CMD_REGISTER(posix_pwm, NULL, "POSIX PWM smoke test", cmd_pwm_test);
#endif /* CONFIG_SHELL */
