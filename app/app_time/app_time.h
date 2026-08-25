#ifndef __APP_TIME_H__
#define __APP_TIME_H__

#include "app_module.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_time.h
 * @brief Wall-clock semantics on top of the RTC driver.
 *
 * Sole writer of the hardware RTC. Publishes EVT_TIME_TICK_15MIN and
 * EVT_TIME_DAY_CHANGED so consumers never re-derive them.
 *
 * The scalar: uint32_t wall clock seconds from 1970-01-01 00:00:00, whose
 * value IS the moment the watch face shows. The phone defines the anchor;
 * this module never adds or subtracts an offset. gmtime_r() is the correct
 * inverse -- not localtime_r/mktime, which consult TZ.
 */
extern const app_module_t app_time_module;

#define APP_TIME_TICK_MIN_STEP  15u

/** Wall clock seconds; 0 if the RTC is unset or unreadable. */
uint32_t app_time_now(void);

/**
 * Store wall clock seconds in the RTC. Uses gmtime_r to expand the scalar
 * into calendar fields -- wday is computed by libc, not guessed.
 * Returns 0 on success, negative on failure.
 */
int app_time_set(uint32_t sec);

/**
 * Expand wall clock seconds into struct tm calendar fields.
 * Caller interprets tm_year+1900, tm_mon+1 per standard struct tm.
 * No-op when out is NULL.
 */
void app_time_to_calendar(uint32_t sec, struct tm *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TIME_H__ */
