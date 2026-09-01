/**
 * @file    hmi_ble_central_shell.c
 * @brief   `blescan` shell command -- drive the BLE observer without a phone or UI.
 *
 * The scanner has no other caller: nothing in the UI opens a device list yet, so
 * without this command the whole observer path is compiled but unreachable.  It
 * is also the only way to see what the radio actually reports, which is the one
 * thing the protocol has no room for.
 *
 * How to read the output during bring-up:
 *   "start" returns ok but list stays empty
 *                     -- either nothing in range matches HMI_CENTRAL_NAME_FILTER
 *                        (default "eBadge", so a phone will NOT show up), or no
 *                        reports are arriving at all.  Widen the filter before
 *                        suspecting the radio: define HMI_CENTRAL_NAME_FILTER to
 *                        "" and the list should fill within a second anywhere.
 *   names shown as (none)
 *                     -- normal.  Many devices advertise a name only in the scan
 *                        response, some never; the entry is still usable, RSSI
 *                        and MAC are always present.
 *   rssi around -30..-50 near, -90..-100 at the edge of range.
 *
 * Note "stop" is a software gate, not a radio stop: on this single-link stack the
 * scan cannot be stopped once started (see hmi_ble_central_start_scan), so the
 * receiver duty cycle stays paid until reset.  That is why nothing starts a scan
 * at boot, and why this command exists rather than an always-on scan.
 */
#if defined(__ZEPHYR__) && defined(CONFIG_SHELL)

#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include "hmi_ble_central.h"

static int cmd_scan_start(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!hmi_ble_central_start_scan())
    {
        shell_error(sh, "start rejected -- a central link is being set up or is up");
        return -EBUSY;
    }
    shell_print(sh, "scanning (list cleared); filter \"%s\"",
                HMI_CENTRAL_NAME_FILTER[0] != '\0' ? HMI_CENTRAL_NAME_FILTER : "(none, all devices)");
    shell_print(sh, "run `blescan list` after a second or two");
    return 0;
}

static int cmd_scan_stop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!hmi_ble_central_stop_scan())
    {
        shell_warn(sh, "not scanning");
        return 0;
    }
    /* Say this plainly: someone reading only the console would otherwise assume
     * the receiver went idle and stop looking for the power draw. */
    shell_print(sh, "reports ignored from now on (radio keeps scanning -- cannot"
                " be stopped on this stack)");
    return 0;
}

static int cmd_scan_list(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint8_t cnt = hmi_ble_central_get_dev_count();

    if (cnt == 0)
    {
        shell_print(sh, "no devices cached (start a scan, or the name filter"
                    " \"%s\" matched nothing)", HMI_CENTRAL_NAME_FILTER);
        return 0;
    }

    shell_print(sh, "%-3s %-17s %-6s %s", "idx", "mac", "rssi", "name");
    for (uint8_t i = 0; i < cnt; i++)
    {
        uint8_t mac[6];
        int8_t  rssi = 0;
        char    name[32] = {0};

        if (!hmi_ble_central_get_dev(i, mac, NULL, &rssi, name, sizeof(name)))
        {
            continue;
        }
        /* MAC is printed high byte first, the order it is shown in on phones and
         * in the Realtek traces -- the array itself is little-endian. */
        shell_print(sh, "%-3u %02X:%02X:%02X:%02X:%02X:%02X %-6d %s",
                    i, mac[5], mac[4], mac[3], mac[2], mac[1], mac[0],
                    rssi, name[0] != '\0' ? name : "(none)");
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(blescan_sub,
                               SHELL_CMD(start, NULL, "Start/restart scanning, clearing the list", cmd_scan_start),
                               SHELL_CMD(stop,  NULL, "Stop caching reports (radio keeps scanning)", cmd_scan_stop),
                               SHELL_CMD(list,  NULL, "List cached devices with RSSI and name", cmd_scan_list),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(blescan, &blescan_sub, "BLE scan (observer role)", NULL);

#endif /* CONFIG_SHELL */
