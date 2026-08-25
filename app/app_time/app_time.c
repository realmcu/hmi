/*
 * app_time.c — wall clock ownership + local-time boundary ticks.
 *
 * Wall-clock ownership: app_time is the SOLE writer of the hardware RTC.
 * External producers (BLE settings command, shell helpers, ...) publish
 * @c EVT_TIME_SYNCED with an @c app_evt_time_synced_t payload; the
 * subscriber below applies it via @c app_time_set_local(). Keeping that
 * path event-driven means new consumers (UI refresh, alarm re-arm) can
 * hook in without any producer having to know about them.
 *
 * Boundary ticks
 * --------------
 * Driven by the RTC's own 1 Hz update interrupt (POSIX_RTC_IOCTL_SET_UPDATE_CB
 * -> RTC_INT_TICK), not by a software timer. The clock that defines the
 * boundaries is therefore the clock that reports them: no deadline to
 * recompute, no drift to correct, and nothing to re-arm when EVT_TIME_SYNCED
 * moves the wall clock.
 *
 * Published on a LOCAL quarter-hour boundary (:00/:15/:30/:45):
 *
 *   EVT_TIME_TICK_15MIN   every boundary
 *   EVT_TIME_DAY_CHANGED  additionally when that boundary is local midnight
 *
 * "Local" means UTC + s_tz_min, defaulting to +480 (Beijing). Aligning to the
 * wall clock rather than to uptime is what lets two devices booted at
 * different moments fire at the same instant, which is why consumers can
 * treat these ticks as authoritative bucket boundaries.
 *
 * ISR discipline: the update callback runs in interrupt context (the Realtek
 * driver invokes it straight from rtc_irq_handler). Only two things happen
 * there — a GET_TIME ioctl, which the port explicitly permits from an ISR and
 * which is pure register reads, and app_event_publish_isr(), the non-blocking
 * queue put. Everything else is left to the subscribers on app_task.
 *
 * Each tick carries the boundary it represents as a uint32_t Unix second, not
 * the moment the subscriber happens to run: dispatch goes through the event
 * queue, so a consumer timestamping data must use the payload.
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

#define APP_TIME_TICK_STEP_SEC   (APP_TIME_TICK_MIN_STEP * 60u)
#define APP_TIME_RTC_PATH        "/dev/rtc0"

/* Timezone offset from UTC in minutes. Beijing until a sync says otherwise. */
static int16_t s_tz_min = 480;

/* Held open for the lifetime of the process: the update callback is bound to
 * this fd, and closing it would tear the subscription down. */
static posix_fd_t s_rtc_fd = POSIX_FD_NULL;

/* Local day index of the last EVT_TIME_DAY_CHANGED, so the event fires once
 * per day even if the clock jumps forward. Touched only from the RTC ISR. */
static uint32_t s_last_day_published;
static bool     s_day_valid;

int app_time_set_local(const app_time_local_t *time)
{
    if (time == NULL)
    {
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

/* --------------------------------------------------------------
 * Clock reads.
 * -------------------------------------------------------------- */

/* Days from 1970-01-01 to the given civil date. Howard Hinnant's algorithm,
 * valid for any year >= 1970 without table lookups or libc. */
static uint32_t civil_to_epoch(const posix_rtc_time_t *t)
{
    if (t->year < 1970 || t->month < 1 || t->month > 12 ||
        t->mday < 1 || t->mday > 31 || t->hour > 23 ||
        t->minute > 59 || t->second > 60)
    {
        return 0;
    }

    int32_t  year = (int32_t)t->year - (t->month <= 2 ? 1 : 0);
    int32_t  era = year / 400;
    uint32_t year_of_era = (uint32_t)(year - era * 400);
    uint32_t month = t->month + (t->month > 2 ? -3u : 9u);
    uint32_t day_of_year = (153u * month + 2u) / 5u + t->mday - 1u;
    uint32_t day_of_era = year_of_era * 365u + year_of_era / 4u -
                          year_of_era / 100u + day_of_year;
    int64_t days = (int64_t)era * 146097 + (int64_t)day_of_era - 719468;

    return (uint32_t)(days * 86400 +
                      (int64_t)t->hour * 3600 +
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
    if (rc != POSIX_OK) { return 0; }

    return civil_to_epoch(&t);
}

/* Local seconds-since-epoch. Only meaningful for deriving local boundaries
 * and day indices — never persist it, persist the UTC value instead.
 *
 * The shift is computed in 64 bits: a 32-bit signed intermediate overflows for
 * any UTC value past 2038-01-19, which would have collapsed local time to 0 and
 * silently stopped the boundary ticks. Saturates instead of wrapping. */
static uint32_t local_sec_from_utc(uint32_t utc_sec)
{
    int64_t shifted = (int64_t)utc_sec + (int64_t)s_tz_min * 60;

    if (shifted < 0) { return 0u; }
    if (shifted > (int64_t)0xFFFFFFFF) { return 0xFFFFFFFFu; }
    return (uint32_t)shifted;
}

/* Inverse of civil_to_epoch(): Unix seconds -> local calendar fields, applying
 * s_tz_min. Delegates the calendar arithmetic to gmtime_r() rather than running
 * the Hinnant algorithm backwards — newlib is already linked and the driver
 * layer already pulls gmtime_r in, so this costs ~70 B against ~240 B for a
 * hand-rolled version, with libc's leap-year handling instead of ours.
 *
 * time_t is 64-bit in this toolchain, so the tz shift cannot overflow for any
 * uint32_t input. Intended for rendering a stored UTC timestamp in logs and on
 * screen — nothing should round-trip through it to do date arithmetic. */
void app_time_to_local(uint32_t utc_sec, app_time_local_t *out)
{
    if (out == NULL)
    {
        return;
    }

    time_t shifted = (time_t)utc_sec + (time_t)s_tz_min * 60;
    struct tm tm_buf;

    if (gmtime_r(&shifted, &tm_buf) == NULL)
    {
        return;
    }

    out->year    = (uint16_t)(tm_buf.tm_year + 1900);
    out->month   = (uint8_t)(tm_buf.tm_mon + 1);
    out->day     = (uint8_t)tm_buf.tm_mday;
    out->hour    = (uint8_t)tm_buf.tm_hour;
    out->min     = (uint8_t)tm_buf.tm_min;
    out->sec     = (uint8_t)tm_buf.tm_sec;
    out->weekday = (uint8_t)tm_buf.tm_wday;
}

/* --------------------------------------------------------------
 * Boundary tick, driven by the RTC's 1 Hz update interrupt.
 * -------------------------------------------------------------- */

/* Read the clock from ISR context. GET_TIME is one of the few commands the
 * port permits there — it is pure register arithmetic with no locking, and
 * carries no posix_port_in_isr() guard, unlike SET_TIME / SET_ALARM. */
static uint32_t rtc_now_from_isr(void)
{
    posix_rtc_time_t t;

    if (s_rtc_fd == POSIX_FD_NULL) { return 0u; }
    if (posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_TIME, &t) != POSIX_OK)
    {
        return 0u;
    }
    return civil_to_epoch(&t);
}

/* RTC second interrupt. Fires once per second regardless of what we do with
 * it, so the common case must be cheap: read the clock, and unless this second
 * happens to be a boundary, return.
 *
 * Runs in interrupt context — no logging, no blocking calls, and publishing
 * goes through the ISR-safe variant which drops rather than waits if the queue
 * is full. */
static void rtc_second_cb(posix_fd_t fd, void *arg)
{
    (void)fd; (void)arg;

    uint32_t now_utc = rtc_now_from_isr();
    if (now_utc == 0u)
    {
        /* Clock not set yet: nothing to align to. Ticks begin once
         * EVT_TIME_SYNCED has given the RTC a real value. */
        return;
    }

    uint32_t local = local_sec_from_utc(now_utc);
    uint32_t local_day = local / 86400u;

    /* The day index is tracked every second rather than only on a boundary, so
     * the rollover is noticed even if the clock jumps across midnight. */
    bool day_rolled = false;
    if (!s_day_valid)
    {
        /* First reading after boot or after a sync: adopt the day silently.
         * Publishing here would ask consumers to clear totals they have only
         * just started accumulating. */
        s_last_day_published = local_day;
        s_day_valid = true;
    }
    else if (local_day != s_last_day_published)
    {
        s_last_day_published = local_day;
        day_rolled = true;
    }

    /* Everything below is boundary-only. */
    if (!day_rolled && (local % APP_TIME_TICK_STEP_SEC) != 0u)
    {
        return;
    }

    /* Day first: a consumer that resets daily totals on the rollover wants to
     * do so before it is handed the bucket that starts the new day. Both land
     * on app_task in publish order. */
    if (day_rolled)
    {
        (void)app_event_publish_isr(EVT_TIME_DAY_CHANGED, &now_utc, sizeof now_utc);
    }

    if ((local % APP_TIME_TICK_STEP_SEC) == 0u)
    {
        (void)app_event_publish_isr(EVT_TIME_TICK_15MIN, &now_utc, sizeof now_utc);
    }
}

/* Bind the 1 Hz callback. The fd stays open for the lifetime of the process
 * because the subscription is tied to it. */
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

    posix_rtc_update_t u =
    {
        .callback = rtc_second_cb,
        .arg      = NULL,
    };
    int rc = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_SET_UPDATE_CB, &u);
    if (rc != POSIX_OK)
    {
        APP_LOGE("RTC SET_UPDATE_CB failed rc=%d; boundary events disabled", rc);
        return -1;
    }
    return 0;
}

/* A clock jump makes the cached day index meaningless: drop it so the next
 * second adopts the new day silently instead of reporting a rollover. */
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

    if (tick_start() != 0)
    {
        /* The module still owns the RTC write path, so stay loaded — only the
         * boundary events are missing. */
        return 0;
    }

    APP_LOGI("app_time init: tz=%+d min, %u-minute boundary ticks off RTC 1Hz",
             (int)s_tz_min, (unsigned)APP_TIME_TICK_MIN_STEP);
    return 0;
}

const app_module_t app_time_module =
{
    .name  = "time",
    .init  = time_init,
};
