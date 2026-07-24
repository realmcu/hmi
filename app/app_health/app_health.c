/*
 * app_health.c — skeleton
 *
 * TODO: subscribe to gsensor-algorithm step updates, drive HR/SpO2
 *       sensor sessions, persist samples via FlashDB TSDB, publish
 *       EVT_HEALTH_*_UPDATED.
 */

#include "app_health.h"

static int health_init(void) { return 0; }

const app_module_t app_health_module =
{
    .name  = "health",
    .init  = health_init,
};
