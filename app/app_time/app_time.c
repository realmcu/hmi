/*
 * app_time.c — skeleton + demo event producer.
 *
 * TODO: read/write RTC via Zephyr rtc driver (or platform equivalent),
 *       run a per-minute local-time tick, publish EVT_TIME_SYNCED / _TICK_MIN.
 *
 * For now this file also acts as the demo publisher for the event bus:
 * the shell command `app_time tick [count]` publishes EVT_TIME_TICK_MIN
 * with a running counter, letting app_time / app_ble / app_health verify
 * their subscription paths. app_time subscribes to its own event id too,
 * so self-delivery (publisher and subscriber on the same module, still
 * routed through app_task) is exercised on every tick.
 */

#include "app_time.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

APP_LOG_MODULE_REGISTER(app_time);



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



