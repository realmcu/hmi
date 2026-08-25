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
 * CHARGE STATE COMES FROM A PIN, NOT FROM THE VOLTAGE
 * ---------------------------------------------------------------------------
 * The charger IC drives P5_2 (pad 38 = GPIOB17) high while it is charging, and
 * that pin is the whole answer -- see charger-status-gpios in
 * boards/rtl87x3g_evb.overlay.  It is deliberately NOT inferred from a rising
 * voltage instead: a couple of samples of ADC noise would then read as
 * "charging" and flicker on the phone.
 *
 * What the pin cannot tell us is FULL.  A charger that has finished releases the
 * line, so "done charging on the cable" and "running on the battery" are the
 * same level, and separating them needs a cable-presence signal this board does
 * not bring out.  So EBADGE_BATT_FULL is never reported -- a 100% DISCHARGING is
 * honest, whereas guessing FULL from percent == 100 would claim a charger state
 * on the evidence of a voltage reading.
 */
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

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

/*----------------------------------------------------------------------------*
 *  Charger status pin
 *
 *  Guarded on DT_NODE_HAS_PROP rather than assumed present: this file also
 *  builds for boards whose overlay does not bring the charger line out, and a
 *  missing property would otherwise be a GPIO_DT_SPEC_GET syntax error rather
 *  than a legible "no pin here".
 *----------------------------------------------------------------------------*/
#define CHG_NODE            DT_PATH(zephyr_user)

#if DT_NODE_HAS_PROP(CHG_NODE, charger_status_gpios)
#define CHG_PIN_PRESENT     1
static const struct gpio_dt_spec s_chg_gpio =
    GPIO_DT_SPEC_GET(CHG_NODE, charger_status_gpios);
#else
#define CHG_PIN_PRESENT     0
#endif

/**
 * Read the charger line, or report "no idea" when there is nothing to read.
 *
 * Configured lazily rather than from an init hook because the three callers sit
 * on three different threads -- the 0x17 BLE handler on l2_task, the main-face UI
 * refresh, and the vbat shell command -- and the earliest of them can run before
 * ebadge_task_init().  gpio_pin_configure_dt() is idempotent, so the unguarded
 * flag below costs at worst one redundant configure if two threads land here at
 * the same moment; nothing observable depends on which of them wins.
 *
 * @return true when @p out_charging holds a real answer.
 */
static bool charger_pin_charging(bool *out_charging)
{
#if CHG_PIN_PRESENT
    static bool s_ready;

    if (!s_ready)
    {
        if (!gpio_is_ready_dt(&s_chg_gpio))
        {
            EBADGE_WARN("port_env: charger-status GPIO not ready");
            return false;
        }
        int rc = gpio_pin_configure_dt(&s_chg_gpio, GPIO_INPUT);
        if (rc != 0)
        {
            EBADGE_WARN1("port_env: charger-status configure failed rc=%d", rc);
            return false;
        }
        s_ready = true;
    }

    /* _dt, so the overlay's GPIO_ACTIVE_HIGH is what defines "1 = charging" --
     * the polarity lives in the devicetree next to the pin number, not here. */
    int level = gpio_pin_get_dt(&s_chg_gpio);
    if (level < 0)
    {
        EBADGE_WARN1("port_env: charger-status read failed rc=%d", level);
        return false;
    }
    *out_charging = (level != 0);
    return true;
#else
    (void)out_charging;
    return false;
#endif
}

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
    bool     charging = false;

    out->mv_valid = ebadge_port_vbat_mv(&mv);
    out->mv       = out->mv_valid ? mv : 0U;
    out->percent  = out->mv_valid ? mv_to_percent(mv) : VBAT_UNKNOWN_PCT;
    /* An unreadable pin reports DISCHARGING, same as a low one.  The wire format
     * has no "unknown" for this field, and of the two states it does have,
     * DISCHARGING is the one that does not promise the user a charger is
     * connected.  The warning inside charger_pin_charging() is what distinguishes
     * the two cases in the log. */
    out->state    = (charger_pin_charging(&charging) && charging)
                    ? EBADGE_BATT_CHARGING
                    : EBADGE_BATT_DISCHARGING;

    if (out->mv_valid)
    {
        /* Raw code alongside the millivolts, because the two disagreeing is the
         * bug worth catching during bring-up: a code stuck at 0 or 0xfff means
         * the ADC is not converting, while a sane code with a silly voltage
         * means ADC_GetRes()'s calibration data is not there. */
        EBADGE_LOG("port_env: vbat=%u mV (adc raw=%u) -> %u%% %s",
                   (unsigned)mv, (unsigned)ebadge_port_vbat_raw(),
                   (unsigned)out->percent,
                   (out->state == EBADGE_BATT_CHARGING) ? "charging"
                   : "discharging");
    }
    else
    {
        EBADGE_WARN1("port_env: no VBAT reading yet -- reporting %u%% as a"
                     " placeholder", (unsigned)VBAT_UNKNOWN_PCT);
    }
    return 0;
}
