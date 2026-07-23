/*
 * app_health.c — skeleton + demo event subscriber.
 *
 * TODO: subscribe to gsensor-algorithm step updates, drive HR/SpO2
 *       sensor sessions, persist samples via FlashDB TSDB, publish
 *       EVT_HEALTH_*_UPDATED.
 *
 * The demo subscription below listens for EVT_TIME_TICK_MIN so we can
 * verify one-to-many delivery in the shell (see app_time.c's `app_time
 * tick` command). Both app_ble and app_health subscribe to the same
 * event id — a single publish should fire both callbacks in a row on
 * app_task .
 */

#include "app_health.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include <stdint.h>

APP_LOG_MODULE_REGISTER(app_health);

/* -------- Demo subscriber callback -------- */

static void on_time_tick(app_event_id_t id, const void *payload, size_t len, void *user)
{
    (void)id; (void)user;
    uint32_t counter = 0;
    if (payload != NULL && len == sizeof(uint32_t))
    {
        counter = *(const uint32_t *)payload;
    }
    APP_LOGI("got EVT_TIME_TICK_MIN counter=%u", counter);
}

static int health_init(void)
{
    int rc = app_event_subscribe(EVT_TIME_TICK_MIN, on_time_tick, NULL);
    if (rc == 0)
    {
        APP_LOGI("subscribed to EVT_TIME_TICK_MIN");
    }
    else
    {
        APP_LOGE("subscribe EVT_TIME_TICK_MIN failed rc=%d", rc);
    }
    return rc;
}

const app_module_t app_health_module =
{
    .name  = "health",
    .init  = health_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

uint32_t app_health_steps_today(void)      { return 0; }
uint8_t  app_health_heart_rate_last(void)  { return 0; }
uint8_t  app_health_spo2_last(void)        { return 0; }
int      app_health_measure_hr_start(void) { return -1; }
int      app_health_measure_hr_stop(void)  { return -1; }
