/*
 * Copyright (c) 2025 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Dashboard BLE (bluetooth_ext / bee stack) subsystem.
 *
 * BLE-specific: le_gap_init, advertising params, bond manager, server init.
 * The task entry and message loop live in the coordinator (dashboard_bt_ext.c).
 */

#ifndef __DASHBOARD_BLE_H__
#define __DASHBOARD_BLE_H__

#include <stdint.h>
#include <stdbool.h>

#include "app_msg.h"
#include "dashboard_key.h"

/**
 * @brief Initialise the BLE GAP / GATT stack.
 *
 * Creates message queues, calls le_gap_init(), registers app callbacks,
 * configures advertising, and registers the HID CC GATT service.
 *
 * @param[in,out]  p_evt_queue  Event queue handle (created by this function).
 * @param[in,out]  p_io_queue   IO message queue handle (created by this function).
 * @return 0 on success, -1 on failure.
 */
int  dashboard_ble_init(void **p_evt_queue, void **p_io_queue);

/**
 * @brief Start BLE advertising.  Call after gap_start_bt_stack().
 */
void dashboard_ble_start(void);

/**
 * @brief Handle a key event (press or release) for BLE HID.
 *
 * Sends a GATT notification with the corresponding HID Consumer Control report.
 * Must be called from the app task context.
 *
 * @param[in]  event  Key event from io_peripheral.
 */
void dashboard_ble_key_handler(key_event_t event);

/**
 * @brief Handle a GAP / IO message for BLE.
 *
 * Dispatches GAP state changes, connection events, pairing/bonding etc.
 * Must be called from the app task context.
 *
 * @param[in]  p_io_msg  IO message to handle.
 */
void dashboard_ble_gap_handler(T_IO_MSG *p_io_msg);

#endif /* __DASHBOARD_BLE_H__ */
