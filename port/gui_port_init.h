/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __GUI_PORT_INIT_H__
#define __GUI_PORT_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "gui_components_init.h"

void gui_port_register_init(gui_init_fn_t fn);

#undef GUI_INIT_BOARD_EXPORT
#define GUI_INIT_BOARD_EXPORT(fn) \
    __attribute__((used)) __attribute__((constructor(101))) \
    void __gui_constr_1_##fn(void) \
    { gui_port_register_init((gui_init_fn_t)(fn)); }

#undef GUI_INIT_PREV_EXPORT
#define GUI_INIT_PREV_EXPORT(fn) \
    __attribute__((used)) __attribute__((constructor(102))) \
    void __gui_constr_2_##fn(void) \
    { gui_port_register_init((gui_init_fn_t)(fn)); }

#undef GUI_INIT_DEVICE_EXPORT
#define GUI_INIT_DEVICE_EXPORT(fn) \
    __attribute__((used)) __attribute__((constructor(103))) \
    void __gui_constr_3_##fn(void) \
    { gui_port_register_init((gui_init_fn_t)(fn)); }

#undef GUI_INIT_VIEW_DESCRIPTOR_REGISTER
#define GUI_INIT_VIEW_DESCRIPTOR_REGISTER(fn) _GUI_CONSTR4(fn)
#define _GUI_CONSTR4(fn) \
    __attribute__((used)) __attribute__((constructor(104))) \
    void __gui_constr_4_##fn(void) \
    { gui_port_register_init((gui_init_fn_t)(fn)); }

#undef GUI_INIT_VIEW_DESCRIPTOR_GET
#define GUI_INIT_VIEW_DESCRIPTOR_GET(fn) _GUI_CONSTR5(fn)
#define _GUI_CONSTR5(fn) \
    __attribute__((used)) __attribute__((constructor(105))) \
    void __gui_constr_5_##fn(void) \
    { gui_port_register_init((gui_init_fn_t)(fn)); }

#undef GUI_INIT_VIEW_CREATE
#define GUI_INIT_VIEW_CREATE(fn) _GUI_CONSTR6(fn)
#define _GUI_CONSTR6(fn) \
    __attribute__((used)) __attribute__((constructor(106))) \
    void __gui_constr_6_##fn(void) \
    { gui_port_register_init((gui_init_fn_t)(fn)); }

#undef GUI_INIT_APP_EXPORT
#define GUI_INIT_APP_EXPORT(fn) \
    __attribute__((used)) __attribute__((constructor(107))) \
    void __gui_constr_7_##fn(void) \
    { gui_port_register_init((gui_init_fn_t)(fn)); }

#ifdef __cplusplus
}
#endif

#endif
