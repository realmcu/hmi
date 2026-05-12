/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui_port_init.h"

#define GUI_MAX_INIT_FUNCS 128

static gui_init_fn_t __gui_init_fn_pool[GUI_MAX_INIT_FUNCS];
static int __gui_init_count = 0;

void gui_port_register_init(gui_init_fn_t fn)
{
    if (__gui_init_count < GUI_MAX_INIT_FUNCS)
    {
        __gui_init_fn_pool[__gui_init_count++] = fn;
    }
}

void __wrap_gui_components_init(void)
{
    for (int i = 0; i < __gui_init_count; i++)
    {
        if (__gui_init_fn_pool[i])
        {
            __gui_init_fn_pool[i]();
        }
    }
}
