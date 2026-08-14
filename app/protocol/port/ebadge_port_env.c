/**
 * @file    ebadge_port_env.c
 * @brief   Environment porting stub.  TODO(port): read the ADC-backed gauge.
 */
#include "ebadge_port_env.h"

int ebadge_port_env_battery(ebadge_batt_t *out)
{
    if (!out) { return -1; }
    /* TODO(port): read real fuel gauge / ADC + charger state pin.         */
    out->percent = 88;
    out->state   = EBADGE_BATT_DISCHARGING;
    return 0;
}
