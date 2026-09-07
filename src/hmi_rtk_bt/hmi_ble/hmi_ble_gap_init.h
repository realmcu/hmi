/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _BLE_GAP_INIT_APP__
#define _BLE_GAP_INIT_APP__

#ifdef __cplusplus
extern "C" {
#endif

void hmi_ble_gap_init(void);

#ifdef CONFIG_REALTEK_APP_NAVI_PROJECTION
/** Refresh manufacturer/address fields after the public address is finalized. */
void hmi_ble_gap_refresh_advertising_data(void);
#endif

/** End of PERIPH_APP
* @}
*/


#ifdef __cplusplus
}
#endif

#endif

