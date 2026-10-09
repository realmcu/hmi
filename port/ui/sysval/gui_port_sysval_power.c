#include "gui_port_sysval.h"

#include "gui_components_init.h"
#include "gui_sysval_adapter.h"
#include <zephyr/kernel.h>

static struct
{
    uint8_t percent;
    bool charging;
    bool valid;
} power_state;
K_MUTEX_DEFINE(power_lock);

void gui_port_sysval_power_update(uint8_t percent, bool charging)
{
    k_mutex_lock(&power_lock, K_FOREVER);
    power_state.percent = percent;
    power_state.valid = percent <= 100u;
    power_state.charging = charging;
    k_mutex_unlock(&power_lock);
}

static void battery_get(gui_sysval_request_t *request)
{
    k_mutex_lock(&power_lock, K_FOREVER);
    uint8_t percent = power_state.percent;
    bool valid = power_state.valid;
    k_mutex_unlock(&power_lock);
    if (!valid)
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
        return;
    }
    gui_sysval_value_t value = gui_sysval_value_int(percent);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static void charging_get(gui_sysval_request_t *request)
{
    k_mutex_lock(&power_lock, K_FOREVER);
    bool charging = power_state.charging;
    bool valid = power_state.valid;
    k_mutex_unlock(&power_lock);
    if (!valid)
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
        return;
    }
    gui_sysval_value_t value = gui_sysval_value_bool(charging);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static gui_sysval_adapter_t battery_adapter =
{
    .key = GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = battery_get,
};
static gui_sysval_adapter_t charging_adapter =
{
    .key = GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = charging_get,
};

static int gui_port_sysval_power_init(void)
{
    gui_sysval_adapter_register(&battery_adapter);
    gui_sysval_adapter_register(&charging_adapter);
    return 0;
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_power_init);
