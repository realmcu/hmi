/**
 * @file    ebadge_port_vbat_shell.c
 * @brief   `vbat` shell command -- read the battery rail without a phone.
 *
 * The 0x17 GET_BATTERY path needs a paired phone to exercise, which is a slow
 * loop for a reading that is either right or obviously wrong.  This prints the
 * same numbers that command would send, plus the two pieces of evidence the
 * protocol has no room for: the raw ADC code and the completed-conversion count.
 *
 * How to read the output during bring-up:
 *   samples == 0            -- the ROM accepted the registration but no ADC
 *                              interrupt ever fired: not converting at all.
 *   raw stuck at 0 or 0xfff -- converting, but the input is rail-to-rail wrong.
 *   plausible raw, silly mV -- ADC_GetRes() has no calibration data behind it.
 *   mv within ~50 mV of a meter on VBAT -- working.
 */
#if defined(CONFIG_SHELL)

#include <zephyr/shell/shell.h>

#include "ebadge_port_vbat.h"
#include "ebadge_port_env.h"

static int cmd_vbat(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    /* Go through port_env rather than reading the cache directly: this is the
     * exact call 0x17 GET_BATTERY makes, so a disagreement between this command
     * and the phone can only be in the handler, not in the measurement. */
    ebadge_batt_t b = {0};

    if (ebadge_port_env_battery(&b) != 0)
    {
        shell_error(sh, "ebadge_port_env_battery() failed");
        return -EIO;
    }

    shell_print(sh, "VBAT (internal ADC channel via ROM adc_mgr)");
    shell_print(sh, "  %-12s %u", "samples", ebadge_port_vbat_samples());

    if (!b.mv_valid)
    {
        shell_warn(sh, "  no conversion has completed yet -- percent below is a"
                   " placeholder, not a measurement");
        shell_print(sh, "  %-12s %u %%", "percent", b.percent);
        return 0;
    }

    shell_print(sh, "  %-12s %u mV", "voltage", b.mv);
    shell_print(sh, "  %-12s %u (0x%03X)", "adc raw", ebadge_port_vbat_raw(),
                ebadge_port_vbat_raw());
    shell_print(sh, "  %-12s %u %%", "percent", b.percent);
    shell_print(sh, "  %-12s %u (0=discharging 1=charging 2=full; stub)",
                "charge", (unsigned)b.state);

    /* Flag the two readings that mean "the number is a lie" so nobody has to
     * remember the thresholds while staring at a console. */
    if (ebadge_port_vbat_raw() == 0U || ebadge_port_vbat_raw() >= 0x0FFFU)
    {
        shell_warn(sh, "  raw code is pinned -- the ADC is not sampling VBAT");
    }
    else if (b.mv < 2500U || b.mv > 4500U)
    {
        shell_warn(sh, "  %u mV is outside any single-cell Li-ion range --"
                   " suspect ADC_GetRes() calibration", b.mv);
    }
    return 0;
}

SHELL_CMD_REGISTER(vbat, NULL, "battery rail voltage (same read as 0x17 GET_BATTERY)",
                   cmd_vbat);

#endif /* CONFIG_SHELL */
