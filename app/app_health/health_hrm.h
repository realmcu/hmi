#ifndef __HEALTH_HRM_H__
#define __HEALTH_HRM_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool sensor_present;
    bool hr_measuring;
    bool hr_valid;
    uint8_t bpm;
    bool spo2_measuring;
    bool spo2_raw_valid;
    uint16_t spo2_red;
    uint16_t spo2_ir;
    int last_error;
} app_health_hrm_state_t;

int health_hrm_start(void);
void health_hrm_set_suspended(bool suspended);

void health_hrm_start_heart_rate(void);
void health_hrm_stop_heart_rate(void);
void health_hrm_start_spo2(void);
void health_hrm_stop_spo2(void);

void health_hrm_get_state(app_health_hrm_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __HEALTH_HRM_H__ */
