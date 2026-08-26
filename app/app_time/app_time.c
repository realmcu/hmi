/*
 * app_time.c — wall clock ownership + boundary ticks.
 *
 * One uint32_t, from 1970-01-01 00:00:00, value = what the watch face shows.
 * The phone produces it; nothing here shifts it. No s_tz_min, no
 * local_sec_from_utc — which is exactly why the day no longer rolls at 16:00.
 */

#include "app_time.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"
#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_rtc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

APP_LOG_MODULE_REGISTER(app_time);

#define APP_TIME_TICK_STEP_SEC  (APP_TIME_TICK_MIN_STEP * 60u)
#define APP_TIME_RTC_PATH       "/dev/rtc0"

static posix_fd_t s_rtc_fd = POSIX_FD_NULL;
static uint32_t   s_last_day_published;
static bool       s_day_valid;

int app_time_set(uint32_t sec)
{
    time_t    when = (time_t)sec;
    struct tm tm_buf;

    if (gmtime_r(&when, &tm_buf) == NULL)
    {
        APP_LOGE("gmtime_r failed sec=%u", (unsigned)sec);
        return -1;
    }

    (void)posix_port_init_all();
    posix_fd_t rtc = posix_open(APP_TIME_RTC_PATH);
    if (rtc == POSIX_FD_NULL)
    {
        APP_LOGE("open %s failed", APP_TIME_RTC_PATH);
        return -1;
    }

    posix_rtc_time_t rtc_time =
    {
        .year   = (uint16_t)(tm_buf.tm_year + 1900),
        .month  = (uint8_t)(tm_buf.tm_mon  + 1),
        .mday   = (uint8_t) tm_buf.tm_mday,
        .hour   = (uint8_t) tm_buf.tm_hour,
        .minute = (uint8_t) tm_buf.tm_min,
        .second = (uint8_t) tm_buf.tm_sec,
        .wday   = (uint8_t) tm_buf.tm_wday,  /* libc computes, never lied */
        .yday   = 0xFFFFu,
        .nsec   = 0u,
    };
    int rc = posix_ioctl(rtc, POSIX_RTC_IOCTL_SET_TIME, &rtc_time);
    posix_close(rtc);
    if (rc != POSIX_OK)
    {
        APP_LOGE("RTC SET_TIME failed rc=%d", rc);
        return -1;
    }

    APP_LOGI("wall clock set %04u-%02u-%02u %02u:%02u:%02u (sec=%u)",
             (unsigned)rtc_time.year, (unsigned)rtc_time.month,
             (unsigned)rtc_time.mday, (unsigned)rtc_time.hour,
             (unsigned)rtc_time.minute, (unsigned)rtc_time.second,
             (unsigned)sec);
    return 0;
}

/* Howard Hinnant algorithm. Kept because newlib has no timegm() and mktime()
 * would reintroduce TZ. This path is read-only (RTC -> scalar). */
static uint32_t civil_to_epoch(const posix_rtc_time_t *t)
{
    if (t->year < 1970 || t->month < 1 || t->month > 12 ||
        t->mday < 1 || t->mday > 31 || t->hour > 23 ||
        t->minute > 59 || t->second > 60)
    {
        return 0;
    }

    int32_t  year = (int32_t)t->year - (t->month <= 2 ? 1 : 0);
    int32_t  era  = year / 400;
    uint32_t yoe  = (uint32_t)(year - era * 400);
    uint32_t m    = t->month + (t->month > 2 ? -3u : 9u);
    uint32_t doy  = (153u * m + 2u) / 5u + t->mday - 1u;
    uint32_t doe  = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    int64_t  days = (int64_t)era * 146097 + (int64_t)doe - 719468;

    return (uint32_t)(days * 86400 +
                      (int64_t)t->hour   * 3600 +
                      (int64_t)t->minute * 60 +
                      (int64_t)t->second);
}

uint32_t app_time_now(void)
{
    posix_fd_t rtc = posix_open(APP_TIME_RTC_PATH);
    if (rtc == POSIX_FD_NULL) { return 0; }

    posix_rtc_time_t t;
    int rc = posix_ioctl(rtc, POSIX_RTC_IOCTL_GET_TIME, &t);
    posix_close(rtc);
    return (rc != POSIX_OK) ? 0 : civil_to_epoch(&t);
}

void app_time_to_calendar(uint32_t sec, struct tm *out)
{
    if (out == NULL) { return; }
    time_t    when = (time_t)sec;
    struct tm tm_buf;
    if (gmtime_r(&when, &tm_buf) != NULL) { *out = tm_buf; }
}

static uint32_t rtc_now_from_isr(void)
{
    posix_rtc_time_t t;
    if (s_rtc_fd == POSIX_FD_NULL) { return 0u; }
    if (posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_TIME, &t) != POSIX_OK) { return 0u; }
    return civil_to_epoch(&t);
}

static void rtc_second_cb(posix_fd_t fd, void *arg)
{
    (void)fd; (void)arg;

    uint32_t now = rtc_now_from_isr();
    if (now == 0u) { return; }

    /* The wall-clock day boundary is calendar midnight. */
    uint32_t day = now / 86400u;

    bool day_rolled = false;
    if (!s_day_valid)
    {
        s_last_day_published = day;
        s_day_valid = true;
    }
    else if (day != s_last_day_published)
    {
        s_last_day_published = day;
        day_rolled = true;
    }

    if (!day_rolled && (now % APP_TIME_TICK_STEP_SEC) != 0u) { return; }

    if (day_rolled)
    {
        (void)app_event_publish_isr(EVT_TIME_DAY_CHANGED, &now, sizeof now);
    }

    if ((now % APP_TIME_TICK_STEP_SEC) == 0u)
    {
        (void)app_event_publish_isr(EVT_TIME_TICK_15MIN, &now, sizeof now);
    }
}

static int tick_start(void)
{
    (void)posix_port_init_all();
    if (s_rtc_fd == POSIX_FD_NULL)
    {
        s_rtc_fd = posix_open(APP_TIME_RTC_PATH);
        if (s_rtc_fd == POSIX_FD_NULL)
        {
            APP_LOGE("open %s failed; boundary events disabled", APP_TIME_RTC_PATH);
            return -1;
        }
    }
    posix_rtc_update_t u = { .callback = rtc_second_cb, .arg = NULL };
    int rc = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_SET_UPDATE_CB, &u);
    if (rc != POSIX_OK)
    {
        APP_LOGE("RTC SET_UPDATE_CB failed rc=%d; boundary events disabled", rc);
        return -1;
    }
    return 0;
}

static void on_evt_time_synced(app_event_id_t id, const void *payload,
                               size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    s_day_valid = false;
    APP_LOGI("clock synced; day tracking re-baselined");
}

static int time_init(void)
{
    if (app_event_subscribe(EVT_TIME_SYNCED, on_evt_time_synced, NULL) != 0)
    {
        APP_LOGE("subscribe EVT_TIME_SYNCED failed");
        return -1;
    }
    if (tick_start() != 0) { return 0; }   /* stay loaded without boundary events */

    APP_LOGI("app_time init: wall clock seconds, %u-min boundary ticks off RTC 1Hz",
             (unsigned)APP_TIME_TICK_MIN_STEP);
    return 0;
}

const app_module_t app_time_module = { .name = "time", .init = time_init };
