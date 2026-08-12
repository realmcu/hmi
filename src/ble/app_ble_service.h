/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_BLE_SERVICE__
#define _APP_BLE_SERVICE__

#ifdef __cplusplus
extern "C"
{
#endif

#include <profile_server.h>
#include <simple_ble_service.h>
#include <bas.h>

/**
 * @brief Initialize and register all application GATT services.
 */
void app_ble_service_init(void);

#ifdef __cplusplus
}
#endif
#endif
