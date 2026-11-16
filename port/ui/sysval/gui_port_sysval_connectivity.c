/*
 * Bluetooth connectivity sysval adapter.
 */

#include "app_ui_sysval.h"

#include "gui_components_init.h"
#include "gui_port_sysval_common.h"
#include "gui_sysval_keys.h"

static void get_connected(gui_sysval_request_t *request)
{
    bool connected;
    if (app_ui_sysval_get_connected(&connected))
    {
        gui_port_sysval_respond_bool(request, connected);
    }
    else
    {
        gui_port_sysval_respond_error(request);
    }
}

static gui_sysval_adapter_t connected_adapter = {
    .key = GUI_SYSVAL_KEY_CONNECTIVITY_BLUETOOTH_CONNECTED,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = get_connected,
};

static int gui_port_sysval_connectivity_init(void)
{
    gui_sysval_adapter_t *const adapters[] = {
        &connected_adapter,
    };
    return gui_port_sysval_register_adapters(
        adapters, sizeof(adapters) / sizeof(adapters[0]));
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_connectivity_init);
