/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_BLE_FLAGS_H_
#define _APP_BLE_FLAGS_H_

/** @defgroup BLE_CONFIG BLE application configuration
 * @brief Compile-time feature switches for the BLE application.
 * @{
 */

/** Maximum number of simultaneous BLE links. */
#define APP_MAX_LINKS 1

/** Enable (1) or disable (0) deep low-power state support. */
#define F_DLPS_EN 0

/** Enable (1) or exclude (0) the generic GATT client and ANCS client. */
#define F_APP_BT_GATT_CLIENT_SUPPORT 0
#define F_APP_BT_ANCS_CLIENT_SUPPORT (F_APP_BT_GATT_CLIENT_SUPPORT & 1)

/** Optional ANCS behavior, gated by ANCS client support. */
#define F_BT_ANCS_APP_FILTER   (F_APP_BT_ANCS_CLIENT_SUPPORT & 1)
#define F_BT_ANCS_GET_APP_ATTR (F_APP_BT_ANCS_CLIENT_SUPPORT & 0)

/** Enable (1) or disable (0) ANCS client debug logging. */
#define F_BT_ANCS_CLIENT_DEBUG (F_APP_BT_ANCS_CLIENT_SUPPORT & 0)

/** Use the extended GATT server API required by the navigation service. */
#define F_APP_GATT_SERVER_EXT_API_SUPPORT 1

/** @} */
#endif
