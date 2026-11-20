/*
 * Application-level haptic policy on top of /dev/motor0.
 *
 * The POSIX port owns the MOT_EN pad, carrier timer and electrical limits.
 * This module only maps product intents (UI clicks, countdown, notification)
 * to short bounded pulses, so UI code never writes a board pin directly.
 */

#include "app_haptic.h"
#include "app_log.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>

#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_motor.h"

APP_LOG_MODULE_REGISTER(app_haptic);

#define HAPTIC_DEVICE_PATH       "/dev/motor0"
#define HAPTIC_UI_FLOOR_MS       25u

typedef struct
{
    uint16_t duration_ms;
    uint8_t strength;
    uint16_t floor_ms;
} haptic_ui_profile_t;

static const haptic_ui_profile_t s_ui_profiles[] =
{
    [APP_HAPTIC_UI_PAGE]      = { 18u, 26u, 90u },
    [APP_HAPTIC_UI_WHEEL]     = {  8u, 20u, 25u },
    [APP_HAPTIC_UI_DIAL]      = { 10u, 22u, 40u },
    [APP_HAPTIC_UI_COUNTDOWN] = { 30u, 30u,  0u },
    [APP_HAPTIC_UI_POPUP]     = { 24u, 26u,  0u },
};

static posix_fd_t s_fd = POSIX_FD_NULL;
static uint32_t s_last_ui_ms[ARRAY_SIZE(s_ui_profiles)];
static bool s_ready;

static int haptic_ensure_open(void)
{
    if (s_fd != POSIX_FD_NULL)
    {
        return 0;
    }
    (void)posix_port_init_all();
    s_fd = posix_open(HAPTIC_DEVICE_PATH);
    if (s_fd == POSIX_FD_NULL)
    {
        return -ENODEV;
    }
    (void)posix_ioctl(s_fd, POSIX_MOTOR_IOCTL_STOP, NULL);
    return 0;
}

static int haptic_init(void)
{
    int rc = haptic_ensure_open();
    s_ready = rc == 0;
    APP_LOGI("ready=%d", (int)s_ready);
    return rc;
}

bool app_haptic_ready(void)
{
    return s_ready;
}

int app_haptic_start(uint8_t strength)
{
    if (haptic_ensure_open() != 0)
    {
        return -ENODEV;
    }
    return posix_ioctl(s_fd, POSIX_MOTOR_IOCTL_START, &strength) == POSIX_OK ? 0 : -EIO;
}

int app_haptic_stop(void)
{
    if (s_fd == POSIX_FD_NULL)
    {
        return 0;
    }
    return posix_ioctl(s_fd, POSIX_MOTOR_IOCTL_STOP, NULL) == POSIX_OK ? 0 : -EIO;
}

int app_haptic_pulse(uint16_t duration_ms, uint8_t strength)
{
    if (haptic_ensure_open() != 0)
    {
        return -ENODEV;
    }
    posix_motor_pulse_t pulse = {
        .duration_ms = duration_ms,
        .strength = strength,
    };
    return posix_ioctl(s_fd, POSIX_MOTOR_IOCTL_PULSE, &pulse) == POSIX_OK ? 0 : -EIO;
}

void app_haptic_ui(app_haptic_ui_t kind)
{
    if ((unsigned)kind >= ARRAY_SIZE(s_ui_profiles))
    {
        return;
    }
    const haptic_ui_profile_t *profile = &s_ui_profiles[kind];
    uint32_t now = k_uptime_get_32();
    if (profile->floor_ms != 0u &&
        now - s_last_ui_ms[kind] < profile->floor_ms)
    {
        return;
    }
    s_last_ui_ms[kind] = now;
    (void)app_haptic_pulse(profile->duration_ms, profile->strength);
}

const app_module_t app_haptic_module =
{
    .name = "haptic",
    .init = haptic_init,
};
