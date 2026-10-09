#include "gui_components_init.h"
#include "gui_sysval_adapter.h"
#include <zephyr/app_version.h>

static void firmware_version_get(gui_sysval_request_t *request)
{
    gui_sysval_value_t value = gui_sysval_value_str(APP_VERSION_STRING);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static gui_sysval_adapter_t firmware_version_adapter =
{
    .key = GUI_SYSVAL_KEY_DEVICE_FIRMWARE_VERSION,
    .type = GUI_SYSVAL_TYPE_STR,
    .get_request = firmware_version_get,
};

static int gui_port_sysval_device_init(void)
{
    return gui_sysval_adapter_register(&firmware_version_adapter) ? 0 : -1;
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_device_init);
