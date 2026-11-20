#ifndef POSIX_IOCTL_PWM_H
#define POSIX_IOCTL_PWM_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* Polarity */
#define POSIX_PWM_POLARITY_NORMAL    0
#define POSIX_PWM_POLARITY_INVERTED  1

/* Config struct */
typedef struct {
    int      channel;
    uint32_t freq_hz;          /* frequency, either this or period_us */
    uint32_t period_us;        /* period (us), takes precedence over freq_hz */
    float    duty_cycle;       /* 0.0 ~ 1.0 */
    uint32_t pulse_us;         /* pulse width (us), takes precedence over duty_cycle */
    uint8_t  polarity;
} posix_pwm_config_t;

/* Capture result callback */
typedef struct {
    int     channel;
    void  (*callback)(posix_fd_t fd, int channel,
                      uint32_t period_us, uint32_t pulse_us, void *arg);
    void   *arg;
} posix_pwm_capture_t;

/* ioctl commands */
#define POSIX_PWM_IOCTL_SET_CONFIG      POSIX_IOC(POSIX_DEVICE_MAGIC_PWM, 1)
#define POSIX_PWM_IOCTL_SET_DUTY        POSIX_IOC(POSIX_DEVICE_MAGIC_PWM, 2)
#define POSIX_PWM_IOCTL_SET_PULSE       POSIX_IOC(POSIX_DEVICE_MAGIC_PWM, 3)
#define POSIX_PWM_IOCTL_START           POSIX_IOC(POSIX_DEVICE_MAGIC_PWM, 4)
#define POSIX_PWM_IOCTL_STOP            POSIX_IOC(POSIX_DEVICE_MAGIC_PWM, 5)
#define POSIX_PWM_IOCTL_CAPTURE_START   POSIX_IOC(POSIX_DEVICE_MAGIC_PWM, 6)
#define POSIX_PWM_IOCTL_CAPTURE_STOP    POSIX_IOC(POSIX_DEVICE_MAGIC_PWM, 7)

#endif
