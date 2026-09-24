#include "app_model.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/app_version.h>

static app_model_snapshot_t s_model =
{
    .firmware_version = APP_VERSION_STRING,
};
static uint32_t s_time_base_uptime_ms;
static uint32_t s_activity_distance_cm_remainder;
static uint32_t s_activity_calories_x100_remainder;
K_MUTEX_DEFINE(s_model_mutex);

static void model_changed(void)
{
    s_model.revision++;
    s_model.updated_ms = k_uptime_get_32();
}

void app_model_get_snapshot(app_model_snapshot_t *out)
{
    if (out == NULL)
    {
        return;
    }

    k_mutex_lock(&s_model_mutex, K_FOREVER);
    *out = s_model;
    if (out->time_valid)
    {
        out->wall_clock_sec +=
            (k_uptime_get_32() - s_time_base_uptime_ms) / 1000u;
    }
    k_mutex_unlock(&s_model_mutex);
}

void app_model_update_time(uint32_t wall_clock_sec)
{
    k_mutex_lock(&s_model_mutex, K_FOREVER);
    s_model.wall_clock_sec = wall_clock_sec;
    s_model.time_valid = (wall_clock_sec != 0u);
    s_time_base_uptime_ms = k_uptime_get_32();
    model_changed();
    k_mutex_unlock(&s_model_mutex);
}

void app_model_update_activity(uint32_t steps, uint32_t distance_m,
                               uint32_t calories_dkcal)
{
    k_mutex_lock(&s_model_mutex, K_FOREVER);
    s_model.steps = steps;
    s_model.distance_m = distance_m;
    s_model.calories_dkcal = calories_dkcal;
    s_activity_distance_cm_remainder = 0u;
    s_activity_calories_x100_remainder = 0u;
    model_changed();
    k_mutex_unlock(&s_model_mutex);
}

void app_model_accumulate_activity_raw(uint32_t steps, uint32_t distance_cm,
                                       uint32_t calories_x100)
{
    k_mutex_lock(&s_model_mutex, K_FOREVER);
    uint32_t total_distance_cm = s_activity_distance_cm_remainder + distance_cm;
    uint32_t total_calories_x100 =
        s_activity_calories_x100_remainder + calories_x100;
    s_model.steps += steps;
    s_model.distance_m += total_distance_cm / 100u;
    s_model.calories_dkcal += total_calories_x100 / 10000u;
    s_activity_distance_cm_remainder = total_distance_cm % 100u;
    s_activity_calories_x100_remainder = total_calories_x100 % 10000u;
    model_changed();
    k_mutex_unlock(&s_model_mutex);
}

void app_model_update_accelerometer(int32_t x_mg, int32_t y_mg, int32_t z_mg)
{
    k_mutex_lock(&s_model_mutex, K_FOREVER);
    s_model.accel_x_mg = x_mg;
    s_model.accel_y_mg = y_mg;
    s_model.accel_z_mg = z_mg;
    s_model.accel_valid = true;
    model_changed();
    k_mutex_unlock(&s_model_mutex);
}

void app_model_update_power(uint8_t battery_percent, bool charging)
{
    k_mutex_lock(&s_model_mutex, K_FOREVER);
    s_model.battery_percent = (battery_percent > 100u) ? 100u : battery_percent;
    s_model.battery_valid = (battery_percent <= 100u);
    s_model.charging = charging;
    model_changed();
    k_mutex_unlock(&s_model_mutex);
}

void app_model_reset_activity(void)
{
    k_mutex_lock(&s_model_mutex, K_FOREVER);
    s_model.steps = 0u;
    s_model.distance_m = 0u;
    s_model.calories_dkcal = 0u;
    s_activity_distance_cm_remainder = 0u;
    s_activity_calories_x100_remainder = 0u;
    model_changed();
    k_mutex_unlock(&s_model_mutex);
}

void app_model_set_accelerometer_offline(void)
{
    k_mutex_lock(&s_model_mutex, K_FOREVER);
    s_model.accel_valid = false;
    model_changed();
    k_mutex_unlock(&s_model_mutex);
}