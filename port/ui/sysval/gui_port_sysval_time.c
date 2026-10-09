#include "gui_port_sysval.h"

#include "app_event.h"
#include "app_event_defs.h"
#include "app_time.h"
#include "gui_components_init.h"
#include "gui_sysval_adapter.h"
#include <zephyr/kernel.h>
#include <limits.h>
#include <time.h>

static bool clock_synced;
K_MUTEX_DEFINE(clock_lock);

static void time_synced_event(app_event_id_t id, const void *payload,
                              size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    k_mutex_lock(&clock_lock, K_FOREVER);
    clock_synced = true;
    k_mutex_unlock(&clock_lock);
}

void gui_port_sysval_time_subscribe(void)
{
    (void)app_event_subscribe(EVT_TIME_SYNCED, time_synced_event, NULL);
}

static void time_get(gui_sysval_request_t *request)
{
    uint32_t now = app_time_now();
    if (now == 0u)
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
        return;
    }
    gui_sysval_value_t value = gui_sysval_value_int((int32_t)(now % 86400u));
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static void date_get(gui_sysval_request_t *request)
{
    uint32_t now = app_time_now();
    if (now == 0u)
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
        return;
    }
    struct tm calendar;
    app_time_to_calendar(now, &calendar);
    gui_sysval_value_t value = gui_sysval_value_int(
                                   (calendar.tm_year + 1900) * 10000 + (calendar.tm_mon + 1) * 100 + calendar.tm_mday);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static void synced_get(gui_sysval_request_t *request)
{
    k_mutex_lock(&clock_lock, K_FOREVER);
    bool synced = clock_synced;
    k_mutex_unlock(&clock_lock);
    gui_sysval_value_t value = gui_sysval_value_bool(synced);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static void epoch_get(gui_sysval_request_t *request)
{
    uint32_t now = app_time_now();
    k_mutex_lock(&clock_lock, K_FOREVER);
    bool synced = clock_synced;
    k_mutex_unlock(&clock_lock);
    if (!synced || now == 0u || now > INT32_MAX)
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
        return;
    }
    gui_sysval_value_t value = gui_sysval_value_int((int32_t)now);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &value);
}

static gui_sysval_adapter_t time_adapter =
{
    .key = GUI_SYSVAL_KEY_SYSTEM_TIME,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = time_get,
};
static gui_sysval_adapter_t date_adapter =
{
    .key = GUI_SYSVAL_KEY_SYSTEM_DATE,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = date_get,
};
static gui_sysval_adapter_t synced_adapter =
{
    .key = GUI_SYSVAL_KEY_SYSTEM_TIME_SYNCED,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = synced_get,
};
static gui_sysval_adapter_t epoch_adapter =
{
    .key = WATCH_SYSVAL_KEY_EPOCH_SECONDS,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = epoch_get,
};

static int gui_port_sysval_time_init(void)
{
    gui_sysval_adapter_register(&time_adapter);
    gui_sysval_adapter_register(&date_adapter);
    gui_sysval_adapter_register(&synced_adapter);
    gui_sysval_adapter_register(&epoch_adapter);
    return 0;
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_time_init);
