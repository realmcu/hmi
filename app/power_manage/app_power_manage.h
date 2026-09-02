/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef APP_POWER_MANAGE_H_
#define APP_POWER_MANAGE_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_POWER_LONG_PRESS_MS 3000U

bool app_power_manage_is_power_off_requested(void);
void app_power_manage_handle_long_press(void);

void gui_port_reset_power_off_request(void);
void gui_port_power_off(void);
bool gui_port_power_on_gate(void);
void gui_port_power_down_enter(void);
void screen_on_off_power_set(bool on);

#ifdef __cplusplus
}
#endif

#endif /* APP_POWER_MANAGE_H_ */
