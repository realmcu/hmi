/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */
#ifndef __APP_KEY_BUTTON_H
#define __APP_KEY_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board-level physical buttons (ADC_2 -> Power, P3_5 -> Home).
 *
 * Configures both pins as digital input + level interrupt + software debounce.
 * On press/release, updates the state and timestamp globals subscribed by the
 * GUI keyboard input device (gui_kb_create).
 */
void app_key_button_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_KEY_BUTTON_H */
