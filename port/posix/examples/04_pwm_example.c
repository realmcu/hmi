/* ================================================================
 * PWM usage example
 *
 * Function: Open PWM0 -> configure 50Hz servo control -> dynamically adjust duty cycle -> close
 *
 * PWM has no streaming data, read/write returns NOSUPP,
 * all operations are done via ioctl.
 *
 * Build requirements: posix.h + posix_ioctl_pwm.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_pwm.h"

void example_pwm(void)
{
    /* === 1. Open device === */
    posix_fd_t pwm = posix_open("/dev/pwm0");
    if (pwm == POSIX_FD_NULL)
    {
        return;
    }

    /* === 2. Configure: 50Hz, 7.5% duty cycle === */
    /* Servo: 50Hz period 20ms
     *   0.5ms pulse width ->  0 degrees  (2.5%)
     *   1.5ms pulse width ->  90 degrees (7.5%)
     *   2.5ms pulse width ->  180 degrees(12.5%) */
    posix_pwm_config_t cfg =
    {
        .channel    = 0,
        .freq_hz    = 50,           /* 20ms period */
        .duty_cycle = 0.075f,       /* 1.5ms pulse width = center */
        .polarity   = POSIX_PWM_POLARITY_NORMAL,
    };
    posix_ioctl(pwm, POSIX_PWM_IOCTL_SET_CONFIG, &cfg);

    /* === 3. Start output === */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_START, &(int) {0}); /* channel 0 */

    /* === 4. Dynamically adjust pulse width (microseconds) === */
    /* Move to 0 degrees */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_SET_PULSE,
    &(posix_pwm_config_t) { .channel = 0, .pulse_us = 500 });
    /* Delay... */

    /* Move to 180 degrees */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_SET_DUTY,
    &(posix_pwm_config_t) { .channel = 0, .duty_cycle = 0.125f });

    /* === 5. Stop output === */
    posix_ioctl(pwm, POSIX_PWM_IOCTL_STOP, &(int) {0});

    /* === 6. Close === */
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
