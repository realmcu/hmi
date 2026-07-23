/*
 * app_ble.c — skeleton + demo event subscriber.
 *
 * TODO: wire up Realtek BLE MGR init, translate stack callbacks into
 *       EVT_BLE_CONNECTED / EVT_BLE_DISCONNECTED events, host the private
 *       GATT service for the companion phone app.
 *
 * The demo subscription below listens for EVT_TIME_TICK_MIN so we can
 * verify one-to-many delivery in the shell (see app_time.c's `app_time
 * tick` command).
 */

#include "app_ble.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include <stdint.h>

APP_LOG_MODULE_REGISTER(app_ble);

/* -------- Demo subscriber callback -------- */

static void on_time_tick(app_event_id_t id, const void *payload, size_t len, void *user)
{
    (void)id; (void)user;
    uint32_t counter = 0;
    if (payload != NULL && len == sizeof(uint32_t))
    {
        /* Payload copy is guaranteed 4-byte aligned inside the queue slot. */
        counter = *(const uint32_t *)payload;
    }
    APP_LOGI("got EVT_TIME_TICK_MIN counter=%u", counter);
}

static int ble_init(void)
{
    /* Subscribe from init() — the contract guarantees every module has
     * been given a chance to subscribe before app_task starts pulling. */
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

const app_module_t app_ble_module =
{
    .name  = "ble",
    .init  = ble_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

bool app_ble_is_connected(void)         { return false; }
int  app_ble_disconnect(void)           { return -1; }
int  app_ble_advertising_start(void)    { return -1; }
int  app_ble_advertising_stop(void)     { return -1; }
