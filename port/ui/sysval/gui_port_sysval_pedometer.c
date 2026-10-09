#include "gui_port_sysval.h"

#include "app_health.h"
#include "gui_components_init.h"
#include "gui_sysval_adapter.h"
#include <limits.h>

static void respond_today(gui_sysval_request_t *request, unsigned field)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);
    uint32_t result = field == 0 ? today.steps :
                      field == 1 ? today.distance_m :
                      field == 2 ? today.calories_dkcal / 10u : today.calories_dkcal;
    if (result > INT32_MAX)
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
        return;
    }
    gui_sysval_value_t value = gui_sysval_value_int((int32_t)result);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static void steps_get(gui_sysval_request_t *request) { respond_today(request, 0); }
static void distance_get(gui_sysval_request_t *request) { respond_today(request, 1); }
static void calories_get(gui_sysval_request_t *request) { respond_today(request, 2); }
static void calories_dkcal_get(gui_sysval_request_t *request) { respond_today(request, 3); }

static gui_sysval_adapter_t steps_adapter =
{
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_STEPS,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = steps_get,
};
static gui_sysval_adapter_t distance_adapter =
{
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_DISTANCE,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = distance_get,
};
static gui_sysval_adapter_t calories_adapter =
{
    .key = GUI_SYSVAL_KEY_HEALTH_PEDOMETER_CALORIES,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = calories_get,
};
static gui_sysval_adapter_t calories_dkcal_adapter =
{
    .key = WATCH_SYSVAL_KEY_CALORIES_DKCAL,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = calories_dkcal_get,
};

static int gui_port_sysval_pedometer_init(void)
{
    gui_sysval_adapter_register(&steps_adapter);
    gui_sysval_adapter_register(&distance_adapter);
    gui_sysval_adapter_register(&calories_adapter);
    gui_sysval_adapter_register(&calories_dkcal_adapter);
    return 0;
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_pedometer_init);
