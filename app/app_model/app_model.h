#ifndef APP_MODEL_H
#define APP_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MODEL_FIRMWARE_VERSION_MAX 32u

/*
 * UI-facing application state. Writers update this model instead of calling
 * UI APIs; readers always receive a complete copy protected by the model's
 * mutex.
 */
typedef struct
{
    uint32_t revision;
    uint32_t updated_ms;
    uint32_t wall_clock_sec;
    uint32_t steps;
    uint32_t distance_m;
    uint32_t calories_dkcal;
    int32_t accel_x_mg;
    int32_t accel_y_mg;
    int32_t accel_z_mg;
    uint8_t battery_percent;
    char firmware_version[APP_MODEL_FIRMWARE_VERSION_MAX];
    bool time_valid;
    bool accel_valid;
    bool battery_valid;
    bool charging;
} app_model_snapshot_t;

void app_model_get_snapshot(app_model_snapshot_t *out);
void app_model_update_time(uint32_t wall_clock_sec);
void app_model_update_activity(uint32_t steps, uint32_t distance_m,
                               uint32_t calories_dkcal);
void app_model_accumulate_activity_raw(uint32_t steps, uint32_t distance_cm,
                                       uint32_t calories_x100);
void app_model_update_accelerometer(int32_t x_mg, int32_t y_mg, int32_t z_mg);
void app_model_update_power(uint8_t battery_percent, bool charging);
void app_model_reset_activity(void);
void app_model_set_accelerometer_offline(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MODEL_H */