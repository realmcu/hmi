#ifndef __APP_POWER_H__
#define __APP_POWER_H__

#include "app_module.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_power.h
 * @brief Battery gauge and charging state.
 *
 * Reads whatever fuel-gauge peripheral the board exposes, samples the
 * charger status pin, publishes @c EVT_POWER_LEVEL periodically,
 * @c EVT_POWER_LOW once on threshold crossing, @c EVT_POWER_CHARGING on
 * every plug/unplug transition.
 */
extern const app_module_t app_power_module;

/** 0..100 percent, or 0 if not yet measured. */
uint8_t app_power_battery_percent(void);

/** true while the charger is plugged in. */
bool    app_power_is_charging(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_POWER_H__ */
