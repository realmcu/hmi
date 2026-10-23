/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/**
 * @file app_power_manage.h
 * @brief eBadge power management public API.
 *
 * Power-off on the 8773g badge is a real SoC deep power-down, not a
 * screen-off: application-controlled rails are cut, PSRAM enters deep
 * power-down, and only the power key remains armed as a wake source.  A key
 * wake-up reboots the SoC; whether the application actually starts is decided
 * at boot by app_power_on_gate(), which requires a long press of the same key.
 *
 * Typical flows
 *   - Power off     : gui_port_power_off()    -> app_power_down_enter()
 *   - Reboot        : app_power_reboot()
 *   - Cancel / wake : gui_port_reset_power_off_request()
 *   - Wake decision : main()                  -> app_power_on_gate()
 *   - Screen state  : app_screen_on_off_power_set() (panel on/off, not power-down)
 */

#ifndef APP_POWER_MANAGE_H_
#define APP_POWER_MANAGE_H_

#include <stdbool.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power-key long press threshold, in milliseconds.
 *
 * The key must be held at least this long to power off, and -- at boot, via
 * app_power_on_gate() -- to power on or wake.
 */
#define APP_POWER_LONG_PRESS_MS 3000U

/**
 * @brief Check whether a power-off request is pending.
 *
 * @return true if the device is in the power-off flow (or already in
 *         power-down), false if it is running normally.
 *
 * @note The flag is set by gui_port_power_off() / app_power_down_enter() and
 *       cleared by gui_port_reset_power_off_request().  The input layer uses it
 *       to interpret the power key: a press while a power-off is pending means
 *       "wake", otherwise it means "power off".
 */
bool app_power_power_off_requested(void);

/**
 * @brief Handle a power-key long press.
 *
 * Called by the input layer once the key has been held for
 * APP_POWER_LONG_PRESS_MS.  The action depends on the current state:
 *
 *   - Power-off pending (screen off but not powered down): wakes the badge by
 *     clearing the AON power-off marker and rebooting (sys_reboot()).
 *   - Running normally: raises the switch-off confirmation UI.
 *
 * @note Must be called from the GUI thread, as it posts to the UI server.
 */
void app_power_manage_handle_long_press(void);

/**
 * @brief Cancel a pending power-off request.
 *
 * Called when the user dismisses the switch-off confirmation dialog.  Clears
 * the flag read by app_power_power_off_requested() so the device returns to
 * normal operation without powering down.
 */
void gui_port_reset_power_off_request(void);

/**
 * @brief Request a full power-off.
 *
 * Initiates the complete power-off sequence: panel off, timers and
 * non-essential threads quiesced, radio parked, power-off marker written, and
 * finally app_power_down_enter().  Safe to call from any GUI callback; the
 * request is routed to the GUI thread.
 *
 * @note Standard entry point for UI confirm and shell power-off.
 */
void gui_port_power_off(void);

void gui_port_reboot(void);

/**
 * @brief Reboot the SoC.
 *
 * Clears the AON power-off marker and performs a cold reset
 * (sys_reboot(SYS_REBOOT_COLD)).  The application restarts from main(); the
 * power-on gate lets it straight through, since the marker is gone and the
 * reset is not a PAD wake.
 *
 * @note Does not return.  This is an immediate reset: it does not quiesce
 *       threads, park the radio or flush application state, so do that first
 *       if it matters.  Also used by app_power_manage_handle_long_press() to
 *       wake from a screen-off state.
 */
FUNC_NORETURN void app_power_reboot(void);

/**
 * @brief Boot-time power-on gate.
 *
 * Called very early in main(), before any application rail is powered.  When
 * the power-off marker is present (i.e. a previous deep power-down), the power
 * key must be held for APP_POWER_LONG_PRESS_MS; a short press, or any non-PAD
 * wake reason, is rejected and the device returns to deep power-down.
 *
 * @return true to continue booting, false to re-enter power-down.
 */
bool app_power_on_gate(void);

/**
 * @brief Enter the deep power-down state.
 *
 * Cuts the pad/rail power, arms the power-key wake path and requests
 * POWER_POWERDOWN_MODE.
 *
 * @note Does not return on success: the SoC powers down at the next idle.
 *       Also callable directly for an immediate power-down that skips the UI
 *       confirmation flow (e.g. a rejected power-on gate).
 */
void app_power_down_enter(void);

/**
 * @brief Power the display panel and its related rails on or off.
 *
 * Drives the LCD panel/QSPI, backlight, touch keys and motor rail.
 *
 * @param on true to power the panel on, false to cut the panel rails.
 *
 * @note This is the panel-level on/off used by the display driver for
 *       screen-state changes and by the power-down path.  It is NOT a full
 *       power-down; use gui_port_power_off() for that.
 */
void app_screen_on_off_power_set(bool on);

#ifdef __cplusplus
}
#endif

#endif /* APP_POWER_MANAGE_H_ */
