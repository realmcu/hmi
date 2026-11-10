/*
 * Sysval adapters backed by the app-layer producer bridge.
 */

#include "app_health.h"
#include "app_time.h"
#include "app_ui_sysval.h"

#include "gui_components_init.h"
#include "gui_sysval.h"
#include "gui_sysval_adapter.h"

#include <stddef.h>

static void respond_int(gui_sysval_request_t *request, int32_t value)
{
    gui_sysval_value_t out = gui_sysval_value_int(value);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &out);
}

static void respond_bool(gui_sysval_request_t *request, bool value)
{
    gui_sysval_value_t out = gui_sysval_value_bool(value);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &out);
}

static void respond_error(gui_sysval_request_t *request)
{
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
}

static void get_battery(gui_sysval_request_t *request)
{
    uint8_t percent;
    if (app_ui_sysval_get_battery(&percent))
    {
        respond_int(request, percent);
    }
    else
    {
        respond_error(request);
    }
}

static void get_charging(gui_sysval_request_t *request)
{
    bool charging;
    if (app_ui_sysval_get_charging(&charging))
    {
        respond_bool(request, charging);
    }
    else
    {
        respond_error(request);
    }
}

static void get_connected(gui_sysval_request_t *request)
{
    bool connected;
    if (app_ui_sysval_get_connected(&connected))
    {
        respond_bool(request, connected);
    }
    else
    {
        respond_error(request);
    }
}

static void get_today(gui_sysval_request_t *request)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);
    respond_int(request, (int32_t)today.steps);
}

static void get_distance(gui_sysval_request_t *request)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);
    respond_int(request, (int32_t)today.distance_m);
}

static void get_calories(gui_sysval_request_t *request)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);
    respond_int(request, (int32_t)((today.calories_dkcal + 5u) / 10u));
}

static void get_step_target(gui_sysval_request_t *request)
{
    respond_int(request, APP_UI_SYSVAL_STEP_TARGET_DEFAULT);
}

static void get_calorie_target(gui_sysval_request_t *request)
{
    respond_int(request, APP_UI_SYSVAL_CALORIE_TARGET_DEFAULT);
}

static void get_unavailable(gui_sysval_request_t *request)
{
    respond_error(request);
}

static void get_date(gui_sysval_request_t *request)
{
    uint32_t now = app_time_now();
    if (now == 0u)
    {
        respond_error(request);
        return;
    }

    struct tm calendar;
    app_time_to_calendar(now, &calendar);
    int32_t date = (calendar.tm_year + 1900) * 10000 +
                   (calendar.tm_mon + 1) * 100 + calendar.tm_mday;
    respond_int(request, date);
}

static void get_time(gui_sysval_request_t *request)
{
    uint32_t now = app_time_now();
    if (now == 0u)
    {
        respond_error(request);
        return;
    }
    respond_int(request, (int32_t)(now % 86400u));
}

static gui_sysval_adapter_t battery_adapter = {
    .key = GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_battery,
};

static gui_sysval_adapter_t charging_adapter = {
    .key = GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = get_charging,
};

static gui_sysval_adapter_t connected_adapter = {
    .key = GUI_SYSVAL_KEY_CONNECTIVITY_BLUETOOTH_CONNECTED,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = get_connected,
};

static gui_sysval_adapter_t steps_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_STEPS,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_today,
};

static gui_sysval_adapter_t distance_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_DISTANCE,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_distance,
};

static gui_sysval_adapter_t calories_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_CALORIES,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_calories,
};

static gui_sysval_adapter_t step_target_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_STEP_TARGET,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_step_target,
};

static gui_sysval_adapter_t calorie_target_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_CALORIE_TARGET,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_calorie_target,
};

static gui_sysval_adapter_t heart_rate_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_HEART_RATE,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_unavailable,
};

static gui_sysval_adapter_t sleep_deep_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_SLEEP_DEEP,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_unavailable,
};

static gui_sysval_adapter_t sleep_light_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_SLEEP_LIGHT,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_unavailable,
};

static gui_sysval_adapter_t date_adapter = {
    .key = GUI_SYSVAL_KEY_SYSTEM_DATE,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_date,
};

static gui_sysval_adapter_t time_adapter = {
    .key = GUI_SYSVAL_KEY_SYSTEM_TIME,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_time,
};

static int gui_port_sysval_init(void)
{
    gui_sysval_adapter_register(&battery_adapter);
    gui_sysval_adapter_register(&charging_adapter);
    gui_sysval_adapter_register(&connected_adapter);
    gui_sysval_adapter_register(&steps_adapter);
    gui_sysval_adapter_register(&distance_adapter);
    gui_sysval_adapter_register(&calories_adapter);
    gui_sysval_adapter_register(&step_target_adapter);
    gui_sysval_adapter_register(&calorie_target_adapter);
    gui_sysval_adapter_register(&heart_rate_adapter);
    gui_sysval_adapter_register(&sleep_deep_adapter);
    gui_sysval_adapter_register(&sleep_light_adapter);
    gui_sysval_adapter_register(&date_adapter);
    gui_sysval_adapter_register(&time_adapter);
    return 0;
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_init);
