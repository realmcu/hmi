#ifndef GUI_PORT_SYSVAL_COMMON_H
#define GUI_PORT_SYSVAL_COMMON_H

#include <stddef.h>

#include "gui_sysval_adapter.h"

static inline void gui_port_sysval_respond_int(gui_sysval_request_t *request,
                                               int32_t value)
{
    gui_sysval_value_t out = gui_sysval_value_int(value);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &out);
}

static inline void gui_port_sysval_respond_bool(gui_sysval_request_t *request,
                                                bool value)
{
    gui_sysval_value_t out = gui_sysval_value_bool(value);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &out);
}

static inline void gui_port_sysval_respond_str(gui_sysval_request_t *request,
                                               const char *value)
{
    gui_sysval_value_t out = gui_sysval_value_str(value);
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_OK, &out);
}

static inline void gui_port_sysval_respond_error(gui_sysval_request_t *request)
{
    gui_sysval_respond(request, GUI_SYSVAL_STATUS_ERROR, NULL);
}

static inline int gui_port_sysval_register_adapters(
    gui_sysval_adapter_t *const *adapters, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (!gui_sysval_adapter_register(adapters[i]))
        {
            return -1;
        }
    }
    return 0;
}

#endif
