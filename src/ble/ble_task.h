/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _BLE_TASK_H_
#define _BLE_TASK_H_

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Create the Zephyr thread that owns BLE event processing.
 *
 * Call this after GAP and GATT service initialization. The thread creates the BLE message queues,
 * starts the stack, and then dispatches GAP and application I/O events.
 */
void ble_task_init(void);

#ifdef __cplusplus
}
#endif
#endif
