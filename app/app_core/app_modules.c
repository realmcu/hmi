/*
 * app_modules.c
 *
 * Central registry of all app-layer modules. This is the ONLY file that
 * includes every business module's header. To add / remove / reorder a
 * module, edit this file and its CMakeLists (auto-glob picks up new
 * subdirectories automatically once they own a CMakeLists.txt).
 *
 * Order is dependency order — earlier entries come up first.
 */

#include "app_module.h"

#include "app_ble.h"
#include "app_time.h"
#include "app_power.h"
#include "app_setting.h"
#include "app_health.h"
#include "app_media.h"
#include "app_phone.h"
#include "app_notify.h"

#include <stddef.h>

const app_module_t *const app_module_list[] =
{
    &app_time_module,       /* clock/timezone, others timestamp against it */
    &app_power_module,      /* battery monitor, come up early */
    &app_setting_module,    /* KV backing, others read defaults during init */
    &app_ble_module,        /* stack ready before higher profiles */
    &app_health_module,     /* depends on time + setting */
    &app_media_module,      /* depends on ble */
    &app_phone_module,      /* depends on ble */
    &app_notify_module,     /* depends on ble */
};

const size_t app_module_count =
    sizeof(app_module_list) / sizeof(app_module_list[0]);
