#ifndef __APP_HEALTH_H__
#define __APP_HEALTH_H__

#include "app_module.h"
#include "app_health_internal.h"   /* health_daily_rollup_t + health_pedo_record_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_health.h
 * @brief Aggregation layer for pedometer, heart rate, SpO2 and future
 *        sleep / workout data.
 *
 * Lifecycle
 * ---------
 * The module registers itself in app_modules.c. app_core calls its
 * init(); init() subscribes to time, binding and power events and returns
 * immediately. The gsa acquisition task starts only after both time sync
 * and user binding have completed.
 *
 * Persistence
 * -----------
 * Runtime samples are aggregated in a 15-minute bucket (fixed by product
 * spec) and appended to the "pedo" FlashDB TSDB. The current-day rollup
 * is mirrored to the "env" FlashDB KVDB (key "health.today") so UI /
 * BLE consumers can read today's totals in O(1) without touching TSDB.
 *
 * Consumer API
 * ------------
 * Callers outside app_health should only use the read-only helpers
 * below plus the EVT_HEALTH_* events. Writes go through the acquisition
 * path and are internal.
 */

extern const app_module_t app_health_module;

/**
 * @brief  Copy of the current UTC-day rollup (steps/distance/calories).
 *
 * Copies the latest in-memory rollup. Zero-initialised until the worker
 * is seeded or records its first bucket. */
void app_health_get_today(health_daily_rollup_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_HEALTH_H__ */
