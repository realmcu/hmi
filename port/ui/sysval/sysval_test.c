/*
 * Hardware diagnostics for the app-to-HoneyGUI sysval bridge.
 */

#include "app_health.h"
#include "app_power.h"
#include "app_time.h"
#include "app_ui_sysval.h"
#include "gui_sysval_keys.h"
#include "ui_sysval.h"

#include "charger_api.h"
#include "charger_utils.h"

#include <zephyr/shell/shell.h>

static int cmd_sysval_get(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;

    uint8_t battery;
    bool charging;
    bool connected;
    uint16_t battery_mv = 0;
    int16_t battery_ma = 0;
    T_CHARGER_UTILS_ERROR voltage_status;
    T_CHARGER_UTILS_ERROR current_status;
    health_daily_rollup_t today;
    uint32_t now = app_time_now();
    struct tm calendar;

    app_health_get_today(&today);

    if (app_power_get_battery(&battery))
    {
        shell_print(sh, "power/battery/level = %u", battery);
    }
    else
    {
        shell_print(sh, "power/battery/level = unavailable");
    }
    if (app_power_get_charging(&charging))
    {
        shell_print(sh, "power/battery/charging = %s", charging ? "true" : "false");
    }
    else
    {
        shell_print(sh, "power/battery/charging = unavailable");
    }
    voltage_status = charger_utils_get_batt_volt(&battery_mv);
    current_status = charger_utils_get_batt_curr(&battery_ma);
    shell_print(sh, "power/battery/voltage = %u mV (status=%d)",
                (unsigned)battery_mv, (int)voltage_status);
    shell_print(sh, "power/battery/current = %d mA (status=%d)",
                (int)battery_ma, (int)current_status);
    shell_print(sh, "power/charger/state = %d error = %d",
                (int)charger_api_get_charger_state(),
                (int)charger_api_get_error_code());
    if (app_ui_sysval_get_connected(&connected))
    {
        shell_print(sh, "connectivity/bluetooth/connected = %s",
                    connected ? "true" : "false");
    }
    else
    {
        shell_print(sh, "connectivity/bluetooth/connected = unavailable");
    }

    shell_print(sh, "health/pedometer/steps = %u", (unsigned)today.steps);
    shell_print(sh, "health/pedometer/distance = %u", (unsigned)today.distance_m);
    shell_print(sh, "health/pedometer/calories = %u",
                (unsigned)((today.calories_dkcal + 5u) / 10u));
    shell_print(sh, "health/pedometer/step_target = %u",
                APP_UI_SYSVAL_STEP_TARGET_DEFAULT);
    shell_print(sh, "health/pedometer/calorie_target = %u",
                APP_UI_SYSVAL_CALORIE_TARGET_DEFAULT);
    shell_print(sh, "health/heart_rate = unavailable");
    shell_print(sh, "health/sleep/deep = unavailable");
    shell_print(sh, "health/sleep/light = unavailable");
    if (now != 0u)
    {
        app_time_to_calendar(now, &calendar);
        int32_t date = (calendar.tm_year + 1900) * 10000 +
                       (calendar.tm_mon + 1) * 100 + calendar.tm_mday;
        shell_print(sh, "system/time = %u", now % 86400u);
        shell_print(sh, "system/date = %d", date);
    }
    else
    {
        shell_print(sh, "system/time = unavailable");
        shell_print(sh, "system/date = unavailable");
    }
    return 0;
}

static int cmd_sysval_publish(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    app_ui_sysval_publish_all();
    shell_print(sh, "published current app values to HoneyGUI sysval");
    return 0;
}

static int cmd_sysval_cache(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;

    int32_t value;
    bool flag;
    const char *text;

    if (ui_sysval_int(GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL, &value))
    {
        shell_print(sh, "cache power/battery/level = %d", value);
    }
    else
    {
        shell_print(sh, "cache power/battery/level = unavailable");
    }
    if (ui_sysval_bool(GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING, &flag))
    {
        shell_print(sh, "cache power/battery/charging = %s", flag ? "true" : "false");
    }
    else
    {
        shell_print(sh, "cache power/battery/charging = unavailable");
    }
    if (ui_sysval_bool(GUI_SYSVAL_KEY_CONNECTIVITY_BLUETOOTH_CONNECTED, &flag))
    {
        shell_print(sh, "cache connectivity/bluetooth/connected = %s",
                    flag ? "true" : "false");
    }
    else
    {
        shell_print(sh, "cache connectivity/bluetooth/connected = unavailable");
    }
    if (ui_sysval_int(GUI_SYSVAL_KEY_HEALTH_PEDOMETER_STEPS, &value))
    {
        shell_print(sh, "cache health/pedometer/steps = %d", value);
    }
    else
    {
        shell_print(sh, "cache health/pedometer/steps = unavailable");
    }
    if (ui_sysval_int(GUI_SYSVAL_KEY_HEALTH_PEDOMETER_DISTANCE, &value))
    {
        shell_print(sh, "cache health/pedometer/distance = %d", value);
    }
    else
    {
        shell_print(sh, "cache health/pedometer/distance = unavailable");
    }
    if (ui_sysval_int(GUI_SYSVAL_KEY_HEALTH_PEDOMETER_CALORIES, &value))
    {
        shell_print(sh, "cache health/pedometer/calories = %d", value);
    }
    else
    {
        shell_print(sh, "cache health/pedometer/calories = unavailable");
    }
    if (ui_sysval_str(GUI_SYSVAL_KEY_DEVICE_FIRMWARE_VERSION, &text))
    {
        shell_print(sh, "cache device/firmware_version = %s", text);
    }
    else
    {
        shell_print(sh, "cache device/firmware_version = unavailable");
    }
    if (ui_sysval_int(GUI_SYSVAL_KEY_SYSTEM_DATE, &value) &&
        ui_sysval_int(GUI_SYSVAL_KEY_SYSTEM_TIME, &value))
    {
        struct tm calendar;
        if (ui_sysval_time_get(&calendar) != NULL)
        {
            shell_print(sh, "cache system/time = %02d:%02d:%02d",
                        calendar.tm_hour, calendar.tm_min, calendar.tm_sec);
            shell_print(sh, "cache system/date = %04d%02d%02d",
                        calendar.tm_year + 1900, calendar.tm_mon + 1,
                        calendar.tm_mday);
        }
    }
    else
    {
        shell_print(sh, "cache system/time = unavailable");
        shell_print(sh, "cache system/date = unavailable");
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sysval_cmds,
    SHELL_CMD(get, NULL, "Print current app-backed sysval values", cmd_sysval_get),
    SHELL_CMD(publish, NULL, "Publish current values to HoneyGUI sysval", cmd_sysval_publish),
    SHELL_CMD(cache, NULL, "Print values cached by the UI sysval consumer", cmd_sysval_cache),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sysval, &sysval_cmds, "Sysval bridge diagnostics", NULL);
