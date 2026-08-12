/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "app_ble_client.h"
#include "bt_gatt_client.h"
#if F_APP_BT_ANCS_CLIENT_SUPPORT
#include "ancs_sample.h"
#endif

void app_ble_client_init(void)
{
#if F_APP_BT_GATT_CLIENT_SUPPORT
    gatt_client_init(GATT_CLIENT_DISCOV_MODE_REG_SVC_BIT | GATT_CLIENT_DISCOV_MODE_USE_EXT_CLIENT);

#if F_APP_BT_ANCS_CLIENT_SUPPORT
    app_ancs_client_init();
#endif
#endif
}
