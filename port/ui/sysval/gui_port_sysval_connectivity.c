#include "gui_port_sysval.h"

#include "gui_components_init.h"
#include "gui_sysval_adapter.h"
#include <zephyr/kernel.h>

static bool ble_connected;
K_MUTEX_DEFINE(ble_lock);

void gui_port_sysval_ble_update(bool connected)
{
    k_mutex_lock(&ble_lock, K_FOREVER);
    ble_connected = connected;
    k_mutex_unlock(&ble_lock);
}

static void connected_get(gui_sysval_request_t *request)
{
    k_mutex_lock(&ble_lock, K_FOREVER);
    bool connected = ble_connected;
    k_mutex_unlock(&ble_lock);
    gui_sysval_value_t value = gui_sysval_value_bool(connected);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static gui_sysval_adapter_t connected_adapter =
{
    .key = GUI_SYSVAL_KEY_CONNECTIVITY_BLUETOOTH_CONNECTED,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = connected_get,
};

static int gui_port_sysval_connectivity_init(void)
{
    gui_sysval_adapter_register(&connected_adapter);
    return 0;
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_connectivity_init);
