/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_SPP_OTA_H_
#define _APP_SPP_OTA_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register SPP connect/disconnect/rx callbacks for OTA.
 *         Must be called after app_ota_init() and hmi_bt_spp_init().
 */
void app_spp_ota_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _APP_SPP_OTA_H_ */
