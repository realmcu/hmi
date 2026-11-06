/*
 * App-to-HoneyGUI sysval bridge.
 *
 * App modules stay independent of HoneyGUI. This port subscribes to their
 * events and republishes values on the GUI-thread-safe sysval bus.
 */

#include "app_ui_sysval.h"

#include "app_event.h"
#include "app_event_defs.h"
#include "app_health.h"
#include "app_log.h"
#include "app_power.h"
#include "hmi_ble_ctrl.h"
#include "gui_sysval.h"

#include <os_sched.h>
#include <os_sync.h>

static bool s_connected;
static bool s_connected_valid;

APP_LOG_MODULE_REGISTER(app_ui_sysval);

static void publish_int(const char *key, int32_t value)
{
    gui_sysval_value_t v = gui_sysval_value_int(value);
    (void)gui_sysval_publish(key, &v);
}

static void publish_bool(const char *key, bool value)
{
    gui_sysval_value_t v = gui_sysval_value_bool(value);
    (void)gui_sysval_publish(key, &v);
}

static void publish_health_today(void)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);

    int32_t kcal = (int32_t)((today.calories_dkcal + 5u) / 10u);
    publish_int(GUI_SYSVAL_KEY_HEALTH_PEDOMETER_STEPS, (int32_t)today.steps);
    publish_int(GUI_SYSVAL_KEY_HEALTH_PEDOMETER_DISTANCE,
                (int32_t)today.distance_m);
    publish_int(GUI_SYSVAL_KEY_HEALTH_PEDOMETER_CALORIES, kcal);
}

static void on_power_level(app_event_id_t id, const void *payload,
                           size_t len, void *user)
{
    (void)id; (void)user;
    if (payload == NULL || len != sizeof(app_evt_power_level_t))
    {
        return;
    }

    const app_evt_power_level_t *ev = payload;
    publish_int(GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL, ev->percent);
}

static void on_power_charging(app_event_id_t id, const void *payload,
                              size_t len, void *user)
{
    (void)id; (void)user;
    if (payload == NULL || len != sizeof(app_evt_power_charging_t))
    {
        return;
    }

    const app_evt_power_charging_t *ev = payload;
    publish_bool(GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING, ev->charging);
}

static void on_health_updated(app_event_id_t id, const void *payload,
                              size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    publish_health_today();
}

static void set_connected(bool connected)
{
    uint32_t key = os_lock();
    s_connected = connected;
    s_connected_valid = true;
    os_unlock(key);
    publish_bool(GUI_SYSVAL_KEY_CONNECTIVITY_BLUETOOTH_CONNECTED, connected);
}

static void on_ble_connected(app_event_id_t id, const void *payload,
                             size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    set_connected(true);
}

static void on_ble_disconnected(app_event_id_t id, const void *payload,
                                size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    set_connected(false);
}

bool app_ui_sysval_get_battery(uint8_t *percent)
{
    return app_power_get_battery(percent);
}

bool app_ui_sysval_get_charging(bool *charging)
{
    return app_power_get_charging(charging);
}

bool app_ui_sysval_get_connected(bool *connected)
{
    if (connected == NULL) { return false; }

    uint32_t key = os_lock();
    bool valid = s_connected_valid;
    *connected = s_connected;
    os_unlock(key);
    return valid;
}

void app_ui_sysval_publish_all(void)
{
    uint8_t battery;
    bool charging;
    bool connected;

    if (app_power_get_battery(&battery))
    {
        publish_int(GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL, battery);
    }
    if (app_power_get_charging(&charging))
    {
        publish_bool(GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING, charging);
    }
    if (app_ui_sysval_get_connected(&connected))
    {
        publish_bool(GUI_SYSVAL_KEY_CONNECTIVITY_BLUETOOTH_CONNECTED, connected);
    }
    publish_health_today();
}

static int app_ui_sysval_init(void)
{
    if (app_event_subscribe(EVT_POWER_LEVEL, on_power_level, NULL) != 0 ||
        app_event_subscribe(EVT_POWER_CHARGING, on_power_charging, NULL) != 0 ||
        app_event_subscribe(EVT_HEALTH_STEPS_UPDATED, on_health_updated, NULL) != 0 ||
        app_event_subscribe(EVT_BLE_CONNECTED, on_ble_connected, NULL) != 0 ||
        app_event_subscribe(EVT_BLE_DISCONNECTED, on_ble_disconnected, NULL) != 0)
    {
        APP_LOGE("sysval bridge subscription failed");
        return -1;
    }

    set_connected(hmi_ble_ctrl_is_connected());
    uint8_t battery;
    bool charging;
    if (app_power_get_battery(&battery))
    {
        publish_int(GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL, battery);
    }
    if (app_power_get_charging(&charging))
    {
        publish_bool(GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING, charging);
    }
    APP_LOGI("sysval bridge ready");
    return 0;
}

const app_module_t app_ui_sysval_module =
{
    .name = "ui_sysval",
    .init = app_ui_sysval_init,
};
