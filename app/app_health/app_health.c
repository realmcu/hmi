/*
 * app_health.c
 *
 * Health module lifecycle. Owns the plumbing that ties health_worker
 * (private acquisition task) and health_db (FlashDB facade) into the
 * app-layer event bus.
 *
 * Arming sequence
 * ---------------
 * The acquisition worker only starts after BOTH preconditions have been
 * satisfied since boot:
 *
 *   A) EVT_TIME_SYNCED — the phone has set the wall clock, so every
 *      persisted record can carry a real UTC timestamp.
 *   B) EVT_USER_BOUND  — the phone has completed the bind handshake,
 *      i.e. the user has explicitly opted in to letting the device
 *      collect and store their activity data.
 *
 * Either event may arrive first; each handler records its own flag and
 * then attempts to arm. The subscriptions remain active so a later bind
 * can start a worker that has already stopped.
 *
 * Responsibilities:
 *  1. init() — subscribe to the two arming events plus EVT_POWER_LOW,
 *              warm up the store, and return. No worker is running yet.
 *  2. on_evt_time_synced() — mark s_time_synced, attempt arm.
 *  3. on_evt_user_bound()  — mark s_user_bound, attempt arm.
 *  4. on_evt_power_low()   — stop the worker; a graceful stop runs a
 *              final flush before yielding the gsa slot.
 *  5. app_health_on_flush() — called from the worker after each
 *              successful TSDB append. Persists the new today rollup to
 *              env KV and publishes EVT_HEALTH_STEPS_UPDATED. Runs on
 *              the worker task; must not block on the event bus.
 *
 * Deliberately NOT here: the history read cursor (health_db.c, next to the
 * append path whose timestamp ordering it depends on) and the today rollup
 * (health_worker.c, which owns the accumulator). This file only forwards to
 * them so outside consumers include app_health.h alone.
 */

#include "app_health.h"
#include "app_health_internal.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

APP_LOG_MODULE_REGISTER(app_health);

/* Both events land on app_task, so these lifecycle flags are serialised. */
static bool s_time_synced;
static bool s_user_bound;

/* Thin forwarders. The read cursor and the today rollup live in health_db and
 * health_worker respectively; these exist so consumers outside the module only
 * ever include app_health.h. */

int app_health_history_read(health_pedo_record_t *out)
{
    return health_db_read_next(out);
}

void app_health_get_today(health_daily_rollup_t *out)
{
    if (out == NULL) { return; }
    health_worker_get_today(out);
}

/* Runs on the WORKER task, not app_task. Keep it short and non-blocking:
 * one KV write + one event publish. The event bus copy is bounded to
 * APP_EVENT_MAX_PAYLOAD (32B) so the u32 total fits comfortably. */
void app_health_on_flush(const health_daily_rollup_t *rollup)
{
    if (rollup == NULL) { return; }

    if (health_db_save_today(rollup) != 0)
    {
        APP_LOGW("today KV save failed (continuing)");
    }

    uint32_t today_steps = rollup->steps;
    int rc = app_event_publish(EVT_HEALTH_STEPS_UPDATED,
                               &today_steps, sizeof(today_steps));
    if (rc != 0)
    {
        APP_LOGW("publish EVT_HEALTH_STEPS_UPDATED rc=%d", rc);
    }
}

static void try_start_worker(void)
{
    if (health_worker_is_running() || !s_time_synced || !s_user_bound)
    {
        return;
    }

    /* Seed today rollup from KV so a same-UTC-day reboot resumes the
     * running total. RTC is guaranteed set here (s_time_synced is one
    * of the preconditions), so current_utc_day_index is trustworthy; the DB
     * self-invalidates the KV entry when its stored day differs. */
    uint32_t current_utc_day_index = health_worker_rtc_now_utc() / 86400u;
    health_daily_rollup_t today_rollup = {0};
    (void)health_db_load_today(current_utc_day_index, &today_rollup);

    int rc = health_worker_start(&today_rollup);
    if (rc != 0)
    {
        APP_LOGE("worker start failed rc=%d", rc);
        return;
    }

    APP_LOGI("worker armed: time_synced && user_bound");
}

static void on_evt_time_synced(app_event_id_t id, const void *payload,
                               size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;

    s_time_synced = true;
    APP_LOGI("EVT_TIME_SYNCED received (user_bound=%d)", (int)s_user_bound);
    try_start_worker();
}

static void on_evt_user_bound(app_event_id_t id, const void *payload,
                              size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;

    s_user_bound = true;
    APP_LOGI("EVT_USER_BOUND received (time_synced=%d)", (int)s_time_synced);
    try_start_worker();
}

/* Explicit unbind: the user asked the watch to forget the account. Stop
 * acquiring anything, and wipe today's KV so a subsequent bind by a
 * different user doesn't inherit a stranger's step count. TSDB history
 * is intentionally left alone here — data deletion is a separate policy
 * question and probably belongs to a dedicated cleanup command. */
static void on_evt_user_unbound(app_event_id_t id, const void *payload,
                                size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;

    APP_LOGW("EVT_USER_UNBOUND — stopping worker + clearing today KV");
    s_user_bound = false;
    health_worker_stop(HEALTH_STOP_DISCARD);
}

static void on_evt_power_low(app_event_id_t id, const void *payload,
                             size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    APP_LOGW("EVT_POWER_LOW — stopping health worker");
    health_worker_stop(HEALTH_STOP_FLUSH);
}

static int health_init(void)
{
    /* The worker must not start without a usable, synchronized store. */
    if (health_db_init() != 0)
    {
        APP_LOGE("health_db_init failed");
        return -1;
    }
    if (health_worker_init() != 0)
    {
        APP_LOGE("health_worker_init failed");
        return -1;
    }

    if (app_event_subscribe(EVT_TIME_SYNCED, on_evt_time_synced, NULL) != 0)
    {
        APP_LOGE("subscribe EVT_TIME_SYNCED failed");
        return -1;
    }
    if (app_event_subscribe(EVT_USER_BOUND, on_evt_user_bound, NULL) != 0)
    {
        APP_LOGE("subscribe EVT_USER_BOUND failed");
        return -1;
    }
    if (app_event_subscribe(EVT_USER_UNBOUND, on_evt_user_unbound, NULL) != 0)
    {
        APP_LOGE("subscribe EVT_USER_UNBOUND failed");
        /* Not fatal — worker still arms normally, we just won't react to
         * an explicit unbind. */
    }
    if (app_event_subscribe(EVT_POWER_LOW, on_evt_power_low, NULL) != 0)
    {
        APP_LOGE("subscribe EVT_POWER_LOW failed");
        /* Not fatal — recording still works, just no low-battery guard. */
    }

    APP_LOGI("app_health init: awaiting EVT_TIME_SYNCED && EVT_USER_BOUND");
    return 0;
}

const app_module_t app_health_module =
{
    .name = "health",
    .init = health_init,
};
