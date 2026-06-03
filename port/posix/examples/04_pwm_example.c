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
    if (!pwm)
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
