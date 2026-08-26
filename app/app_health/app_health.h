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
 * spec) and appended to the "pedo" FlashDB TSDB — that is the durable
 * history. Today's running totals are RAM-only and restart at zero after a
 * reboot; app_health_get_today() reads them in O(1) without touching TSDB.
 *
 * Consumer API
 * ------------
 * Callers outside app_health should only use the read-only helpers
 * below plus the EVT_HEALTH_* events. Writes go through the acquisition
 * path and are internal.
 */

extern const app_module_t app_health_module;

/**
 * @brief  Copy of the current wall-clock-day rollup (steps/distance/calories).
 *
 * Copies the latest in-memory rollup. Zero-initialised until the worker
 * is seeded or records its first bucket. */
void app_health_get_today(health_daily_rollup_t *out);

/**
 * @brief  Take the next unread activity record, oldest first.
 *
 * Records are handed out once and only once — the read position is a
 * watermark in env KV, so it survives reboot and a phone receives each
 * 15-minute bucket exactly once. Reading does not consume: the records
 * themselves stay in the TSDB until rollover overwrites them, so a full-range
 * iteration still sees everything regardless of the watermark.
 *
 * No open/close: the watermark is the session. Take as many or as few as
 * you like; stopping early leaves the rest for the next call.
 *
 * @warning The watermark never rewinds, and no API rewinds it: a record you
 *          take but fail to deliver cannot be re-read. Take one at a time if
 *          delivery can fail. Recovering from a bad watermark currently means
 *          erasing the env KV partition — there is no reset entry point.
 *
 * @param out  Receives the record; untouched unless the return value is 1.
 * @return 1 = got a record, 0 = nothing unread left, negative errno on a
 *         NULL argument or unusable store.
 */
int app_health_history_read(health_pedo_record_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_HEALTH_H__ */
