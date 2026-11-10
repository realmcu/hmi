#ifndef APP_UI_SYSVAL_H
#define APP_UI_SYSVAL_H

#include "app_module.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const app_module_t app_ui_sysval_module;

#define APP_UI_SYSVAL_STEP_TARGET_DEFAULT    10000
#define APP_UI_SYSVAL_CALORIE_TARGET_DEFAULT 500

bool app_ui_sysval_get_battery(uint8_t *percent);
bool app_ui_sysval_get_charging(bool *charging);
bool app_ui_sysval_get_connected(bool *connected);
void app_ui_sysval_publish_all(void);

#ifdef __cplusplus
}
#endif

#endif
