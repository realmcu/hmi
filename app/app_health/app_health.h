#ifndef __APP_HEALTH_H__
#define __APP_HEALTH_H__

#include "app_module.h"
#include "app_health_internal.h"   /* health_daily_rollup_t + health_pedo_record_t */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/**
 * @brief  Count persisted activity records in a UTC time range.
 *
 * Use this before reading to size a buffer, to decide how many batches a
 * full read needs, or simply to test whether any history exists.
 *
 * Beware on this platform: a record is 18 B, so a store holding a few
 * hundred of them already exceeds a task stack. Treat the count as loop
 * arithmetic over a fixed-size buffer, not as an array dimension.
 *
 * @param from_ts_utc Inclusive lower bound; zero starts at the oldest record.
 * @param to_ts_utc   Inclusive upper bound; zero means no upper bound.
 * @return Number of records (zero if the range holds none), negative errno
 *         for a reversed range or an unavailable store.
 */
int app_health_count_history(uint32_t from_ts_utc, uint32_t to_ts_utc);

/**
 * @brief  Read persisted activity records, oldest first.
 *
 * Record timestamps are strictly increasing (health_db_append_pedo
 * guarantees it), so a batched read advances by taking the next
 * @p from_ts_utc from `out_records[n - 1].ts_utc + 1`.
 *
 * @param from_ts_utc  Inclusive lower bound; zero starts at the oldest record.
 * @param to_ts_utc    Inclusive upper bound; zero means no upper bound.
 * @param out_records  Caller-owned output array, filled in ascending ts_utc.
 * @param max_records  Capacity of @p out_records; further records are left
 *                     in the store for a subsequent call.
 * @return Number of records written, negative errno for invalid arguments
 *         or an unavailable store.
 */
int app_health_read_history(uint32_t from_ts_utc,
                            uint32_t to_ts_utc,
                            health_pedo_record_t *out_records,
                            size_t max_records);

#ifdef __cplusplus
}
#endif

#endif /* __APP_HEALTH_H__ */
