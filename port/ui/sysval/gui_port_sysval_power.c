/*
 * Battery and charging sysval adapters.
 */

#include "app_ui_sysval.h"

#include "gui_components_init.h"
#include "gui_port_sysval_common.h"
#include "gui_sysval_keys.h"

static void get_battery(gui_sysval_request_t *request)
{
    uint8_t percent;
    if (app_ui_sysval_get_battery(&percent))
    {
        gui_port_sysval_respond_int(request, percent);
    }
    else
    {
        gui_port_sysval_respond_error(request);
    }
}

static void get_charging(gui_sysval_request_t *request)
{
    bool charging;
    if (app_ui_sysval_get_charging(&charging))
    {
        gui_port_sysval_respond_bool(request, charging);
    }
    else
    {
        gui_port_sysval_respond_error(request);
    }
}

static gui_sysval_adapter_t battery_adapter = {
    .key = GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_battery,
};

static gui_sysval_adapter_t charging_adapter = {
    .key = GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = get_charging,
};

static int gui_port_sysval_power_init(void)
{
    gui_sysval_adapter_t *const adapters[] = {
        &battery_adapter,
        &charging_adapter,
    };
    return gui_port_sysval_register_adapters(
        adapters, sizeof(adapters) / sizeof(adapters[0]));
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_power_init);
