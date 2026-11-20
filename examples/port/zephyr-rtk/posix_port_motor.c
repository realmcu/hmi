/*
 * POSIX motor port for the MOY.M81023.01 vibration motor.
 *
 * MOT_EN is P1_0 and drives the gate of a P-channel high-side switch. The
 * signal is active low: LOW means motor on, HIGH means motor off. P1_0 does
 * not have a verified hardware-PWM mapping on this SoC, so intensity uses a
 * small software PWM clocked by a HAL timer, like the panel backlight.
 */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_motor.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "hw_tim.h"
#include "rtl876x_pinmux.h"

#define MOTOR_PIN                 P1_0
#define MOTOR_PWM_STEPS           16u
#define MOTOR_PWM_HZ              1000u
#define MOTOR_PWM_STEP_US         (1000000u / (MOTOR_PWM_HZ * MOTOR_PWM_STEPS))
#define MOTOR_MAX_STRENGTH        60u
#define MOTOR_MAX_PULSE_MS        1000u

typedef struct
{
    bool in_use;
} motor_file_t;

static motor_file_t s_motor_file;
static T_HW_TIMER_HANDLE s_timer;
static struct k_work_delayable s_stop_work;
static struct k_mutex s_lock;
static volatile uint8_t s_duty;
static volatile uint8_t s_step;
static uint8_t s_strength;
static bool s_pwm_ready;
static bool s_initialized;

static inline void motor_pad_write(PAD_OUTPUT_VAL level)
{
    Pad_OutputControlValue(MOTOR_PIN, level);
}

static void motor_pwm_tick(T_HW_TIMER_HANDLE handle)
{
    (void)handle;
    uint8_t step = (uint8_t)(s_step + 1u);
    if (step >= MOTOR_PWM_STEPS)
    {
        step = 0u;
    }
    s_step = step;
    motor_pad_write(step < s_duty ? PAD_OUT_LOW : PAD_OUT_HIGH);
}

static bool motor_timer_start(void)
{
    if (s_timer == NULL)
    {
        s_timer = hw_timer_create("posix_motor", MOTOR_PWM_STEP_US, true,
                                  motor_pwm_tick);
        if (s_timer == NULL)
        {
            return false;
        }
        s_pwm_ready = true;
    }
    return hw_timer_start(s_timer);
}

static void motor_timer_stop(void)
{
    if (s_timer != NULL)
    {
        (void)hw_timer_stop(s_timer);
    }
}

static void motor_apply_locked(uint8_t strength)
{
    if (strength > MOTOR_MAX_STRENGTH)
    {
        strength = MOTOR_MAX_STRENGTH;
    }
    s_strength = strength;

    if (strength == 0u)
    {
        s_duty = 0u;
        motor_timer_stop();
        s_step = 0u;
        motor_pad_write(PAD_OUT_HIGH);
        return;
    }

    uint32_t duty = ((uint32_t)strength * MOTOR_PWM_STEPS + 50u) / 100u;
    if (duty == 0u)
    {
        duty = 1u;
    }
    if (duty >= MOTOR_PWM_STEPS)
    {
        duty = MOTOR_PWM_STEPS - 1u;
    }
    s_duty = (uint8_t)duty;
    s_step = 0u;
    motor_pad_write(PAD_OUT_LOW);

    if (!motor_timer_start())
    {
        /* Without a timer the safe fallback is full-on for the requested
         * interval, bounded by the pulse duration below. */
        s_duty = MOTOR_PWM_STEPS;
        motor_pad_write(PAD_OUT_LOW);
    }
}

static void motor_stop_work(struct k_work *work)
{
    (void)work;
    (void)k_mutex_lock(&s_lock, K_FOREVER);
    motor_apply_locked(0u);
    (void)k_mutex_unlock(&s_lock);
}

static void *motor_open(void *drv, const char *path)
{
    (void)drv;
    (void)path;
    if (!s_initialized)
    {
        return POSIX_OPEN_ERR;
    }
    if (s_motor_file.in_use)
    {
        return POSIX_OPEN_ERR;
    }
    s_motor_file.in_use = true;
    return &s_motor_file;
}

static int motor_close(void *drv, void *file)
{
    (void)drv;
    (void)file;
    (void)k_work_cancel_delayable(&s_stop_work);
    (void)k_mutex_lock(&s_lock, K_FOREVER);
    motor_apply_locked(0u);
    (void)k_mutex_unlock(&s_lock);
    s_motor_file.in_use = false;
    return POSIX_OK;
}

static posix_ssize_t motor_read(void *drv, void *file, void *buf, size_t count)
{
    (void)drv;
    (void)file;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t motor_write(void *drv, void *file, const void *buf, size_t count)
{
    (void)drv;
    (void)file;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

static int motor_ioctl(void *drv, void *file, unsigned long cmd, void *arg)
{
    (void)drv;
    (void)file;

    switch (cmd)
    {
    case POSIX_MOTOR_IOCTL_START:
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        (void)k_work_cancel_delayable(&s_stop_work);
        (void)k_mutex_lock(&s_lock, K_FOREVER);
        motor_apply_locked(*(const uint8_t *)arg);
        (void)k_mutex_unlock(&s_lock);
        return POSIX_OK;

    case POSIX_MOTOR_IOCTL_STOP:
        (void)k_work_cancel_delayable(&s_stop_work);
        (void)k_mutex_lock(&s_lock, K_FOREVER);
        motor_apply_locked(0u);
        (void)k_mutex_unlock(&s_lock);
        return POSIX_OK;

    case POSIX_MOTOR_IOCTL_PULSE:
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        {
            const posix_motor_pulse_t *pulse = arg;
            uint16_t duration = pulse->duration_ms;
            if (duration == 0u)
            {
                return POSIX_ERR_INVAL;
            }
            if (duration > MOTOR_MAX_PULSE_MS)
            {
                duration = MOTOR_MAX_PULSE_MS;
            }
            (void)k_work_cancel_delayable(&s_stop_work);
            (void)k_mutex_lock(&s_lock, K_FOREVER);
            motor_apply_locked(pulse->strength);
            (void)k_mutex_unlock(&s_lock);
            (void)k_work_reschedule(&s_stop_work, K_MSEC(duration));
        }
        return POSIX_OK;

    case POSIX_MOTOR_IOCTL_SET_STRENGTH:
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        (void)k_mutex_lock(&s_lock, K_FOREVER);
        motor_apply_locked(*(const uint8_t *)arg);
        (void)k_mutex_unlock(&s_lock);
        return POSIX_OK;

    default:
        return POSIX_ERR_NOSUPP;
    }
}

static const posix_driver_ops_t s_motor_ops =
{
    .open = motor_open,
    .close = motor_close,
    .read = motor_read,
    .write = motor_write,
    .ioctl = motor_ioctl,
};

static int motor_init(void)
{
    if (k_mutex_init(&s_lock) != 0)
    {
        return POSIX_ERR_NOMEM;
    }
    k_work_init_delayable(&s_stop_work, motor_stop_work);

    Pinmux_Deinit(MOTOR_PIN);
    Pad_Config(MOTOR_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE,
               PAD_OUT_ENABLE, PAD_OUT_HIGH);
    (void)Pad_HighSpeedMuxSel(MOTOR_PIN, FROM_AON_DOMAIN);
    motor_pad_write(PAD_OUT_HIGH);
    s_initialized = true;

    return posix_device_register("/dev/motor0", &s_motor_ops, NULL);
}
POSIX_INIT_DEVICE_EXPORT(motor_init);
