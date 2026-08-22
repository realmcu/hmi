/**
 * @file    ebadge_port_env.c
 * @brief   Battery reporting for 0x17 GET_BATTERY.
 *
 * The voltage comes from ebadge_port_vbat.c (internal VBAT channel on the ROM
 * aux ADC).  What this file adds is the mapping to the 0..100 percent the wire
 * format wants, plus a fallback for the window before the first conversion
 * lands.
 *
 * ---------------------------------------------------------------------------
 * VOLTAGE -> PERCENT IS A STRAIGHT LINE, DELIBERATELY
 * ---------------------------------------------------------------------------
 * A single-cell Li-ion discharge curve is not linear, and the vendor watch app
 * carries two 101-entry lookup tables for it (see
 * watch/src/dev/charger/cj4056_charger.c, battery_voltage_table).  Those tables
 * are not copied here yet, on purpose: they were fitted to that board's cell and
 * load, and using them before this board's own readings have been checked
 * against a meter would dress up an unverified voltage as a precise percentage.
 *
 * So the map is linear between the same endpoints the vendor table uses, which
 * is honest about being approximate and is good enough to tell a full battery
 * from an empty one.  Replace it with a real curve once the voltage itself is
 * trusted -- the endpoints below are the place to start.
 *
 * ---------------------------------------------------------------------------
 * CHARGE STATE IS STILL A STUB
 * ---------------------------------------------------------------------------
 * Reporting it needs either the ROM charger module (charger_api_get_charger_state)
 * or an external charger's status pin, and this port was asked for voltage only.
 * DISCHARGING is reported unconditionally, which is at least the common case; it
 * is NOT inferred from a rising voltage, because a couple of samples of noise
 * would then read as "charging" and flicker on the phone.
 */
#include <stdint.h>

#include "ebadge_port_env.h"
#include "ebadge_port_vbat.h"
#include "../ebadge_log.h"

/** Endpoints of the linear map, millivolts.  Taken from the discharge table in
 *  cj4056_charger.c so the two agree at the ends even though the middle
 *  differs. */
#define VBAT_FULL_MV        4125u
#define VBAT_EMPTY_MV       3500u

/** Reported while no conversion has completed yet.
 *
 *  Not 0: the first GET_BATTERY can arrive within a couple of hundred ms of the
 *  phone connecting, before the first ADC sample, and 0% renders as a flat
 *  battery -- a wrong answer that looks like a hardware fault.  Not 100 either,
 *  for the same reason in reverse.  The accompanying log line says plainly that
 *  the number is a placeholder.                                              */
#define VBAT_UNKNOWN_PCT    50u

static uint8_t mv_to_percent(uint16_t mv)
{
    if (mv >= VBAT_FULL_MV)
    {
        return 100u;
    }
    if (mv <= VBAT_EMPTY_MV)
    {
        return 0u;
    }
    /* Multiply before dividing, and in 32 bits: the numerator reaches ~62500,
     * which overflows a uint16_t. */
    uint32_t span = VBAT_FULL_MV - VBAT_EMPTY_MV;
    uint32_t up   = (uint32_t)mv - VBAT_EMPTY_MV;

    return (uint8_t)((up * 100u) / span);
}

int ebadge_port_env_battery(ebadge_batt_t *out)
{
    if (out == NULL)
    {
        return -1;
    }

    uint16_t mv = 0;

    out->mv_valid = ebadge_port_vbat_mv(&mv);
    out->mv       = out->mv_valid ? mv : 0U;
    out->percent  = out->mv_valid ? mv_to_percent(mv) : VBAT_UNKNOWN_PCT;
    /* TODO(port): real charge state -- needs charger_api_get_charger_state() or
     * an external charger status pin.  See the file header. */
    out->state    = EBADGE_BATT_DISCHARGING;

    if (out->mv_valid)
    {
        /* Raw code alongside the millivolts, because the two disagreeing is the
         * bug worth catching during bring-up: a code stuck at 0 or 0xfff means
         * the ADC is not converting, while a sane code with a silly voltage
         * means ADC_GetRes()'s calibration data is not there. */
        EBADGE_LOG("port_env: vbat=%u mV (adc raw=%u) -> %u%%",
                   (unsigned)mv, (unsigned)ebadge_port_vbat_raw(),
                   (unsigned)out->percent);
    }
    else
    {
        EBADGE_WARN1("port_env: no VBAT reading yet -- reporting %u%% as a"
                     " placeholder", (unsigned)VBAT_UNKNOWN_PCT);
    }
    return 0;
}
