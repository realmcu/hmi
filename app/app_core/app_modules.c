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

#include "app_time.h"
#include "app_power.h"
#include "app_setting.h"
#include "app_haptic.h"
#include "app_record.h"
#include "app_health.h"
#include "app_media.h"
#include "app_phone.h"
#include "app_notify.h"

#include <stddef.h>

extern const app_module_t app_bluetooth_module;
extern const app_module_t app_watch_l2_module;
extern const app_module_t app_ui_sysval_module;

const app_module_t *const app_module_list[] =
{
    &app_time_module,       /* clock/timezone, others timestamp against it */
    &app_power_module,      /* battery monitor, come up early */
    &app_setting_module,    /* KV backing, others read defaults during init */
    &app_record_module,     /* record event subscriptions, before UI fills slots */
    &app_haptic_module,     /* vibration motor, independent of audio stack */
    &app_media_module,      /* register A2DP/AVRCP hooks before BT task starts */
    &app_phone_module,      /* phone state/event bridge, before BT task starts */
    &app_bluetooth_module,        /* stack ready before higher profiles */
    &app_watch_l2_module,         /* depends on ble */
    &app_health_module,     /* depends on time + setting */
    &app_ui_sysval_module,  /* bridge app events to HoneyGUI sysval */
    &app_notify_module,     /* depends on ble */
};

const size_t app_module_count =
    sizeof(app_module_list) / sizeof(app_module_list[0]);
