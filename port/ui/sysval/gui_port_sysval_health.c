/*
 * Pedometer and health sysval adapters.
 */

#include "app_health.h"
#include "app_ui_sysval.h"

#include "gui_components_init.h"
#include "gui_port_sysval_common.h"
#include "gui_sysval_keys.h"

static void get_today(gui_sysval_request_t *request)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);
    gui_port_sysval_respond_int(request, (int32_t)today.steps);
}

static void get_distance(gui_sysval_request_t *request)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);
    gui_port_sysval_respond_int(request, (int32_t)today.distance_m);
}

static void get_calories(gui_sysval_request_t *request)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);
    gui_port_sysval_respond_int(request,
                                (int32_t)((today.calories_dkcal + 5u) / 10u));
}

static void get_step_target(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_int(request, APP_UI_SYSVAL_STEP_TARGET_DEFAULT);
}

static void get_calorie_target(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_int(request, APP_UI_SYSVAL_CALORIE_TARGET_DEFAULT);
}

static void get_unavailable(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_error(request);
}

static void get_heart_rate(gui_sysval_request_t *request)
{
    uint8_t bpm = 0;
    if (app_health_get_heart_rate(&bpm, NULL))
    {
        gui_port_sysval_respond_int(request, bpm);
    }
    else
    {
        gui_port_sysval_respond_error(request);
    }
}

static void get_heart_rate_measuring(gui_sysval_request_t *request)
{
    bool measuring = false;
    (void)app_health_get_heart_rate(NULL, &measuring);
    gui_port_sysval_respond_bool(request, measuring);
}

static void set_heart_rate_measuring(const gui_sysval_value_t *value,
                                     gui_sysval_request_t *request)
{
    const bool measuring = value != NULL && value->data.b;
    if (measuring)
    {
        app_health_start_heart_rate();
    }
    else
    {
        app_health_stop_heart_rate();
    }
    gui_port_sysval_respond_bool(request, measuring);
}

static void get_spo2_measuring(gui_sysval_request_t *request)
{
    bool measuring = false;
    (void)app_health_get_spo2_raw(NULL, NULL, &measuring);
    gui_port_sysval_respond_bool(request, measuring);
}

static void set_spo2_measuring(const gui_sysval_value_t *value,
                               gui_sysval_request_t *request)
{
    const bool measuring = value != NULL && value->data.b;
    if (measuring)
    {
        app_health_start_spo2();
    }
    else
    {
        app_health_stop_spo2();
    }
    gui_port_sysval_respond_bool(request, measuring);
}

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
    .get_request = get_heart_rate,
};

static gui_sysval_adapter_t heart_rate_measuring_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_HEART_RATE_MEASURING,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = get_heart_rate_measuring,
    .set_request = set_heart_rate_measuring,
};

static gui_sysval_adapter_t spo2_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_SPO2,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_unavailable,
};

static gui_sysval_adapter_t spo2_measuring_adapter = {
    .key = GUI_SYSVAL_KEY_HEALTH_SPO2_MEASURING,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = get_spo2_measuring,
    .set_request = set_spo2_measuring,
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

static int gui_port_sysval_health_init(void)
{
    gui_sysval_adapter_t *const adapters[] = {
        &steps_adapter,
        &distance_adapter,
        &calories_adapter,
        &step_target_adapter,
        &calorie_target_adapter,
        &heart_rate_adapter,
        &heart_rate_measuring_adapter,
        &spo2_adapter,
        &spo2_measuring_adapter,
        &sleep_deep_adapter,
        &sleep_light_adapter,
    };
    return gui_port_sysval_register_adapters(
        adapters, sizeof(adapters) / sizeof(adapters[0]));
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_health_init);
