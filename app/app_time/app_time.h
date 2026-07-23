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
 * Owns the timezone offset, the "sync from phone" entry point, a
 * once-per-minute tick event and local-time formatting for the UI.
 * Alarms / calendar do not live here in the skeleton — they belong to a
 * future @c app_alarm brainstorm.
 */
extern const app_module_t app_time_module;

/** Wall-clock time in Unix seconds. */
uint32_t app_time_now(void);

/** Timezone offset from UTC, minutes. Beijing = +480. */
int16_t  app_time_tz_offset(void);
int      app_time_tz_set(int16_t minutes);

/**
 * @brief  Set the wall clock from a phone sync payload.
 *
 * Invoked by the private GATT time-sync handler in @c app_ble . Publishes
 * @c EVT_TIME_SYNCED on success.
 */
int      app_time_set_from_phone(uint32_t unix_sec, int16_t tz_min);

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

/** Fill @p out with the current local time (applying the timezone offset). */
void app_time_local_now(app_time_local_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TIME_H__ */
