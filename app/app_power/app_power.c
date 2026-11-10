/*
 * app_power.c
 *
 * Battery/charger producer. The charger callback may run outside app_task,
 * so it only updates a small locked cache and publishes app events. The UI
 * sysval bridge consumes those events and republishes them on HoneyGUI's bus.
 */

#include "app_power.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include "charger_api.h"
#include <os_sched.h>
#include <os_sync.h>

#define POWER_LOW_THRESHOLD 20u

static uint8_t s_battery;
static bool s_battery_valid;
static bool s_charging;
static bool s_charging_valid;
static bool s_low;
static bool s_low_known;
static bool s_ready;

APP_LOG_MODULE_REGISTER(app_power);

static bool charger_state_is_charging(T_CHARGER_STATE state)
{
    return state == STATE_CHARGER_START ||
           state == STATE_CHARGER_PRE_CHARGE ||
           state == STATE_CHARGER_FAST_CHARGE;
}

static void publish_battery(uint8_t percent)
{
    app_evt_power_level_t ev = { .percent = percent };
    (void)app_event_publish(EVT_POWER_LEVEL, &ev, sizeof ev);
}

static void publish_charging(bool charging)
{
    app_evt_power_charging_t ev = { .charging = charging };
    (void)app_event_publish(EVT_POWER_CHARGING, &ev, sizeof ev);
}

static void power_soc_cb(uint8_t soc)
{
    if (soc > 100u)
    {
        uint32_t key = os_lock();
        s_battery_valid = false;
        os_unlock(key);
        return;
    }

    uint32_t key = os_lock();
    bool changed = !s_battery_valid || s_battery != soc;
    bool low_now = soc <= POWER_LOW_THRESHOLD;
    bool low_edge = low_now && (!s_low_known || !s_low);

    s_battery = soc;
    s_battery_valid = true;
    s_low = low_now;
    s_low_known = true;
    bool ready = s_ready;
    os_unlock(key);

    if (!ready)
    {
        return;
    }

    if (changed)
    {
        publish_battery(soc);
    }
    if (low_edge)
    {
        (void)app_event_publish(EVT_POWER_LOW, NULL, 0);
    }
}

static void power_state_cb(T_CHARGER_STATE state)
{
    bool charging = charger_state_is_charging(state);
    uint32_t key = os_lock();
    bool changed = !s_charging_valid || s_charging != charging;
    s_charging = charging;
    s_charging_valid = true;
    bool ready = s_ready;
    os_unlock(key);

    if (ready && changed)
    {
        publish_charging(charging);
    }
}

bool app_power_get_battery(uint8_t *percent)
{
    if (percent == NULL) { return false; }

    uint32_t key = os_lock();
    bool valid = s_battery_valid;
    *percent = s_battery;
    os_unlock(key);
    return valid;
}

bool app_power_get_charging(bool *charging)
{
    if (charging == NULL) { return false; }

    uint32_t key = os_lock();
    bool valid = s_charging_valid;
    *charging = s_charging;
    os_unlock(key);
    return valid;
}

static int power_init(void)
{
    if (charger_api_reg_state_of_charge_callback != NULL)
    {
        charger_api_reg_state_of_charge_callback(power_soc_cb);
    }
    if (charger_api_reg_charger_state_callback != NULL)
    {
        charger_api_reg_charger_state_callback(power_state_cb);
    }

    if (charger_api_get_state_of_charge != NULL)
    {
        uint8_t soc = charger_api_get_state_of_charge();
        uint32_t key = os_lock();
        if (soc <= 100u)
        {
            s_battery = soc;
            s_battery_valid = true;
            s_low = soc <= POWER_LOW_THRESHOLD;
            s_low_known = true;
        }
        os_unlock(key);
    }
    bool charging = false;
    if (charger_api_get_charger_state != NULL)
    {
        charging = charger_state_is_charging(charger_api_get_charger_state());
        uint32_t key = os_lock();
        s_charging = charging;
        s_charging_valid = true;
        os_unlock(key);
    }

    uint32_t key = os_lock();
    s_ready = true;
    uint8_t battery = s_battery;
    bool battery_valid = s_battery_valid;
    bool charging_valid = s_charging_valid;
    os_unlock(key);

    APP_LOGI("power ready: battery=%u valid=%d charging=%d valid=%d",
             (unsigned)battery, (int)battery_valid,
             (int)charging, (int)charging_valid);
    return 0;
}

const app_module_t app_power_module =
{
    .name  = "power",
    .init  = power_init,
};
