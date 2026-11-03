/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _BLE_INIT_H_
#define _BLE_INIT_H_

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Start the Bluetooth LE stack and everything it depends on.
 *
 * Runs the fixed initialization chain of bond storage, GAP, the GATT services
 * (including the navigation projection service) and finally the BLE event
 * thread that calls gap_start_bt_stack(). Advertising begins as soon as the
 * stack reports ready.
 *
 * Call it once from main(), after app_system_lower_init() has brought the PSRAM
 * up (the projection frame slots live there) and after the GUI server exists,
 * so a frame arriving early already has a consumer.
 */
void app_ble_init(void);

#ifdef __cplusplus
}
#endif
#endif
