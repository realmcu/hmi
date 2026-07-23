/*
 * app_power.c — skeleton
 *
 * TODO: read fuel gauge, edge-detect the charge pin, publish
 *       EVT_POWER_LEVEL / _LOW / _CHARGING.
 */

#include "app_power.h"

static int  power_init(void) { return 0; }
const app_module_t app_power_module =
{
    .name  = "power",
    .init  = power_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

uint8_t app_power_battery_percent(void) { return 0; }
bool    app_power_is_charging(void) { return false; }
