/*
 * app_time.c — skeleton + demo event producer.
 *
 * TODO: run a per-minute local-time tick, publish EVT_TIME_SYNCED / _TICK_MIN.
 *
 * For now this file also acts as the demo publisher for the event bus:
 * the shell command `app_time tick [count]` publishes EVT_TIME_TICK_MIN
 * with a running counter, letting app_time / app_ble / app_health verify
 * their subscription paths. app_time subscribes to its own event id too,
 * so self-delivery (publisher and subscriber on the same module, still
 * routed through app_task) is exercised on every tick.
 *
 * Wall-clock ownership: app_time is the SOLE writer of the hardware RTC.
 * External producers (BLE settings command, shell helpers, ...) publish
 * @c EVT_TIME_SYNCED with an @c app_evt_time_synced_t payload; the
 * subscriber below applies it via @c app_time_set_local(). Keeping that
 * path event-driven means new consumers (UI refresh, alarm re-arm) can
 * hook in without any producer having to know about them.
 */

#include "app_time.h"
#include "app_log.h"

#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_rtc.h"

#include <stdint.h>

APP_LOG_MODULE_REGISTER(app_time);

int app_time_set_local(const app_time_local_t *time)
{
    if (time == NULL)
    {
        return -1;
    }

    (void)posix_port_init_all();
    posix_fd_t rtc = posix_open("/dev/rtc0");
    if (rtc == POSIX_FD_NULL)
    {
        APP_LOGE("open /dev/rtc0 failed");
        return -1;
    }

    posix_rtc_time_t rtc_time =
    {
        .year = time->year,
        .month = time->month,
        .mday = time->day,
        .hour = time->hour,
        .minute = time->min,
        .second = time->sec,
        .wday = time->weekday,
        .yday = 0xFFFFu,
        .nsec = 0u,
    };
    int rc = posix_ioctl(rtc, POSIX_RTC_IOCTL_SET_TIME, &rtc_time);
    posix_close(rtc);
    if (rc != POSIX_OK)
    {
        APP_LOGE("RTC SET_TIME failed rc=%d", rc);
        return -1;
    }

    APP_LOGI("wall clock set %04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)time->year, (unsigned)time->month,
             (unsigned)time->day, (unsigned)time->hour,
             (unsigned)time->min, (unsigned)time->sec);
    return 0;
}



static int time_init(void)
{
    APP_LOGI("app time module init! \n");
    return 0;
}

const app_module_t app_time_module =
{
    .name  = "time",
    .init  = time_init,
};
