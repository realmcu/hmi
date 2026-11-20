#ifndef __APP_HAPTIC_H__
#define __APP_HAPTIC_H__

#include <stdbool.h>
#include <stdint.h>

#include "app_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Vibration strength is a percentage. The board port clamps it to the safe
 * range for the shared 3V3_SENSOR rail. */
typedef enum
{
    APP_HAPTIC_UI_PAGE = 0,
    APP_HAPTIC_UI_WHEEL,
    APP_HAPTIC_UI_DIAL,
    APP_HAPTIC_UI_COUNTDOWN,
    APP_HAPTIC_UI_POPUP,
} app_haptic_ui_t;

extern const app_module_t app_haptic_module;

int app_haptic_start(uint8_t strength);
int app_haptic_stop(void);
int app_haptic_pulse(uint16_t duration_ms, uint8_t strength);
bool app_haptic_ready(void);
void app_haptic_ui(app_haptic_ui_t kind);

#ifdef __cplusplus
}
#endif

#endif /* __APP_HAPTIC_H__ */
