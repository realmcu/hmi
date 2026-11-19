/*
 * Date and time sysval adapters.
 */

#include "app_time.h"
#include "app_log.h"

#include "gui_components_init.h"
#include "gui_port_sysval_common.h"
#include "gui_sysval_keys.h"

APP_LOG_MODULE_REGISTER(sysval_time);

static void get_date(gui_sysval_request_t *request)
{
    uint32_t now = app_time_now();
    if (now == 0u)
    {
        gui_port_sysval_respond_error(request);
        return;
    }

    struct tm calendar;
    app_time_to_calendar(now, &calendar);
    int32_t date = (calendar.tm_year + 1900) * 10000 +
                   (calendar.tm_mon + 1) * 100 + calendar.tm_mday;
    APP_LOGI("GUI get_date now=%u calendar=%04d-%02d-%02d -> %d",
             (unsigned)now, calendar.tm_year + 1900, calendar.tm_mon + 1,
             calendar.tm_mday, (int)date);
    gui_port_sysval_respond_int(request, date);
}

static void get_time(gui_sysval_request_t *request)
{
    uint32_t now = app_time_now();
    if (now == 0u)
    {
        gui_port_sysval_respond_error(request);
        return;
    }
    struct tm calendar;
    app_time_to_calendar(now, &calendar);
    int32_t seconds_since_midnight = (int32_t)(now % 86400u);
    static uint32_t last_log_bucket = UINT32_MAX;
    uint32_t log_bucket = now / 10u;
    if (log_bucket != last_log_bucket)
    {
        last_log_bucket = log_bucket;
        APP_LOGI("GUI get_time now=%u calendar=%04d-%02d-%02d %02d:%02d:%02d "
                 "-> %d",
                 (unsigned)now, calendar.tm_year + 1900, calendar.tm_mon + 1,
                 calendar.tm_mday, calendar.tm_hour, calendar.tm_min,
                 calendar.tm_sec, (int)seconds_since_midnight);
    }
    gui_port_sysval_respond_int(request, seconds_since_midnight);
}

static gui_sysval_adapter_t date_adapter = {
    .key = GUI_SYSVAL_KEY_SYSTEM_DATE,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_date,
};

static gui_sysval_adapter_t time_adapter = {
    .key = GUI_SYSVAL_KEY_SYSTEM_TIME,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_time,
};

static int gui_port_sysval_time_init(void)
{
    gui_sysval_adapter_t *const adapters[] = {
        &date_adapter,
        &time_adapter,
    };
    return gui_port_sysval_register_adapters(
        adapters, sizeof(adapters) / sizeof(adapters[0]));
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_time_init);
