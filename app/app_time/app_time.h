#ifndef __APP_TIME_H__
#define __APP_TIME_H__

#include "app_module.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_time.h
 * @brief Wall-clock semantics on top of the RTC driver.
 *
 * Sole writer of the hardware RTC, and the single source of truth for local
 * wall-clock boundaries: it publishes EVT_TIME_TICK_15MIN and
 * EVT_TIME_DAY_CHANGED so consumers never re-derive them.
 *
 * Alarms / calendar do not live here — they belong to a future @c app_alarm.
 */
extern const app_module_t app_time_module;

/**
 * Boundary tick period, minutes. EVT_TIME_TICK_15MIN fires at every local
 * multiple of this (:00/:15/:30/:45). Exposed because consumers that store
 * data per bucket must agree with it — see the _Static_assert in app_health.c.
 */
#define APP_TIME_TICK_MIN_STEP   15u

/** Wall-clock time in Unix seconds; zero if the RTC is unset or unreadable. */
uint32_t app_time_now(void);

typedef struct
{
    uint16_t year;
    uint8_t  month;    /* 1..12 */
    uint8_t  day;      /* 1..31 */
    uint8_t  hour;     /* 0..23 */
    uint8_t  min;      /* 0..59 */
    uint8_t  sec;      /* 0..59 */
    uint8_t  weekday;  /* 0 = Sunday */
} app_time_local_t;

/** Store a validated local calendar value in the hardware RTC. */
int app_time_set_local(const app_time_local_t *time);

/**
 * @brief  Render Unix seconds as local calendar fields.
 *
 * Applies the module's timezone offset, so the result is what a user would
 * read off the watch face. For display and logging; @c out is left untouched
 * when NULL is passed.
 */
void app_time_to_local(uint32_t utc_sec, app_time_local_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TIME_H__ */
