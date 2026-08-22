/**
 * @file    ebadge_port_env.h
 * @brief   Environmental queries -- battery, misc device info.
 *
 * Deliberately narrow: only what the V1.2 GET_BATTERY / BATTERY commands
 * need.  Everything else (device name, MAC) already has other paths.
 */
#ifndef _EBADGE_PORT_ENV_H_
#define _EBADGE_PORT_ENV_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Charging state -- goes on the wire as 0x18 BATTERY's EB_TLV_BAT_CHARGE. */
typedef enum
{
    EBADGE_BATT_DISCHARGING = 0,
    EBADGE_BATT_CHARGING    = 1,
    EBADGE_BATT_FULL        = 2,
} ebadge_batt_state_t;

typedef struct
{
    uint8_t              percent;    /* 0..100                                  */
    ebadge_batt_state_t  state;

    /* Raw rail voltage, millivolts, and whether it is real.
     *
     * Carried alongside percent rather than instead of it because the wire
     * format only has percent -- this is here so bring-up can see what the
     * percentage was derived from, and so "the ADC is not reading" is
     * distinguishable from "the battery is flat".  mv_valid == false means
     * unknown; mv is then meaningless rather than zero-and-therefore-empty.  */
    uint16_t             mv;
    bool                 mv_valid;
} ebadge_batt_t;

int  ebadge_port_env_battery(ebadge_batt_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_ENV_H_ */
