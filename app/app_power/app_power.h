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
 */
extern const app_module_t app_power_module;

/** Copy the latest battery percentage. False until the charger reports one. */
bool app_power_get_battery(uint8_t *percent);

/** Copy the latest charger state as a boolean. False until first report. */
bool app_power_get_charging(bool *charging);

#ifdef __cplusplus
}
#endif

#endif /* __APP_POWER_H__ */
