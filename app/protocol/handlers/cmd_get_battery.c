/**
 * @file    cmd_get_battery.c
 * @brief   0x17 GET_BATTERY -- respond with 0x18 BATTERY.
 *
 * Spec §4.13 TLVs, both required:
 *   0x01 percent  1B 0..100
 *   0x02 charge   1B EBADGE_BATT_* (0 discharging / 1 charging / 2 full)
 *
 * A read failure answers 0x04 RESULT(FAILED) rather than reporting a bogus
 * 0%, which the App would otherwise render as a flat battery.
 *
 * The percent comes from a VBAT reading -- see port/ebadge_port_vbat.c for how
 * it is measured and port/ebadge_port_env.c for the voltage-to-percent map.
 */
#include <stdint.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../port/ebadge_port_env.h"

void handle_get_battery(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    (void)tlvs; (void)n_tlv;

    ebadge_batt_t b = {0};
    if (ebadge_port_env_battery(&b) != 0)
    {
        EBADGE_WARN("GET_BATTERY: port_env read FAIL -> RESULT FAILED");
        (void)ebadge_l2_result_send(EB_CMD_GET_BATTERY, EB_RESULT_FAILED);
        return;
    }

    /* Clamp rather than trust the port: percent is 0..100 on the wire and a
     * mis-scaled ADC reading would otherwise be seen as a protocol error.
     *
     * Note this clamp cannot be used to detect "no reading" -- the port reports
     * that through mv_valid, not through an out-of-range percent, precisely so
     * that a sentinel value cannot be laundered into a plausible 100%.       */
    uint8_t pct = (b.percent > 100u) ? 100u : b.percent;
    uint8_t chg = (uint8_t)b.state;
    if (chg > EBADGE_BATT_FULL)
    {
        EBADGE_WARN1("GET_BATTERY: bogus state=%d, reporting DISCHARGING", (int)chg);
        chg = (uint8_t)EBADGE_BATT_DISCHARGING;
    }

    /* Log the voltage next to the percent, tagged as outbound: percent is all
     * the phone gets, so when it displays something surprising this line is what
     * says whether the number or the measurement behind it is at fault. */
    if (b.mv_valid)
    {
        EBADGE_LOG(EB_DIR_TO_PHONE "0x18 BATTERY: pct=%u charge=%u"
                   " (vbat=%u mV)", (unsigned)pct, (unsigned)chg,
                   (unsigned)b.mv);
    }
    else
    {
        EBADGE_WARN(EB_DIR_TO_PHONE "0x18 BATTERY: pct=%u charge=%u"
                    " (vbat UNKNOWN -- percent is a placeholder)",
                    (unsigned)pct, (unsigned)chg);
    }

    uint8_t  params[16];
    uint16_t off = 0;
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_BAT_PERCENT, pct);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_BAT_CHARGE,  chg);
    (void)ebadge_l2_notify_send(EB_CMD_BATTERY, params, off);
}
