#ifndef __APP_HEALTH_H__
#define __APP_HEALTH_H__

#include "app_module.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_health.h
 * @brief Aggregation layer for pedometer, heart rate, SpO2 and future
 *        sleep / workout data.
 *
 * Consumes raw output from @c component/gsensor-algorithm/ (steps today),
 * future PPG/SpO2 sensor drivers and future NN algorithms; produces a
 * unified snapshot for UI and a persisted time-series (FlashDB TSDB) for
 * sync to the phone app. Publishes update events; details are read via
 * the getters below.
 */
extern const app_module_t app_health_module;

/** Steps accumulated since local midnight. */
uint32_t app_health_steps_today(void);

/** Most recent heart-rate reading in BPM. 0 means "no valid value yet". */
uint8_t  app_health_heart_rate_last(void);

/** Most recent SpO2 reading as percent. 0 means "no valid value yet". */
uint8_t  app_health_spo2_last(void);

/** User-driven "measure now" for heart rate. */
int  app_health_measure_hr_start(void);
int  app_health_measure_hr_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_HEALTH_H__ */
