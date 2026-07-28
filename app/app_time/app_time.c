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
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

APP_LOG_MODULE_REGISTER(app_time);

int app_time_set_local(const app_time_local_t *time)
{
    if (time == NULL)
    {
        return -1;
    }

    /* TODO: write the hardware RTC through the platform driver.
     * app_time is the sole writer of the RTC; keep that ownership
     * here when the real implementation lands. */
    APP_LOGI("app_time_set_local %04u-%02u-%02u %02u:%02u:%02u (stub)",
             (unsigned)time->year, (unsigned)time->month,
             (unsigned)time->day, (unsigned)time->hour,
             (unsigned)time->min, (unsigned)time->sec);
    return 0;
}



static void on_evt_time_synced(app_event_id_t id, const void *payload,
                               size_t len, void *user)
{
    (void)id; (void)user;

    if (payload == NULL || len != sizeof(app_evt_time_synced_t))
    {
        APP_LOGE("EVT_TIME_SYNCED bad payload len=%u", (unsigned)len);
        return;
    }

    const app_evt_time_synced_t *ev = payload;
    app_time_local_t time;

    time.year    = ev->year;
    time.month   = ev->month;
    time.day     = ev->day;
    time.hour    = ev->hour;
    time.min     = ev->min;
    time.sec     = ev->sec;
    time.weekday = 0u;

    (void)app_time_set_local(&time);
}

static int time_init(void)
{
    APP_LOGI("app time module init! \n");
    (void)app_event_subscribe(EVT_TIME_SYNCED, on_evt_time_synced, NULL);
    return 0;
}

const app_module_t app_time_module =
{
    .name  = "time",
    .init  = time_init,
};

