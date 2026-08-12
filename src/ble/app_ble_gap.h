/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_BLE_GAP__
#define _APP_BLE_GAP__

#ifdef __cplusplus
extern "C"
{
#endif

#include <app_msg.h>
#include <profile_server.h>

extern T_SERVER_ID simp_srv_id;
extern T_SERVER_ID bas_srv_id;

/**
 * @brief Dispatch one BLE-stack I/O message from the BLE task.
 *
 * @param io_msg Message received from the BLE I/O queue.
 */
void app_ble_gap_handle_io_msg(T_IO_MSG io_msg);

/**
 * @brief Handle asynchronous GAP callbacks from the BLE stack.
 *
 * @param cb_type GAP callback event type.
 * @param p_cb_data Event-specific callback data.
 * @return Application result returned to the BLE stack.
 */
T_APP_RESULT app_ble_gap_callback(uint8_t cb_type, void *p_cb_data);

/**
 * @brief Initialize GAP parameters, bonding, the BLE manager, and advertising.
 */
void app_ble_gap_init(void);

#ifdef __cplusplus
}
#endif

#endif
