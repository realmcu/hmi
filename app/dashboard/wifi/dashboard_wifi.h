/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * Licensed under the Realtek License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License from Realtek
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DASHBOARD_WIFI_H
#define DASHBOARD_WIFI_H

/* ============================================================================
 * Dashboard WiFi dispatcher
 *
 * Single worker thread consuming a shared queue:
 *   - SDK events (join_status / dhcp_status) -> internal callback enqueue
 *   - Shell commands (ota_http, ...) -> cmd handler enqueue
 *   - Dispatcher thread executes sequentially (no concurrency issues)
 *
 * Adding a feature: implement run_xxx() + one case in dispatch switch.
 * ============================================================================ */

#include <stdbool.h>
#include "basic_types.h"   /* u16 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi dispatcher thread entry. Never returns.
 * @param param  unused
 */
void dash_board_wifi_task(void *param);

/**
 * @brief Check if WiFi/IP is available.
 * @retval true  ready to serve (STA associated + IP assigned, or SoftAP up)
 * @retval false otherwise
 */
bool dashboard_wifi_is_online(void);

/**
 * @brief Report whether the device runs as SoftAP (phone connects to us).
 * @retval true  SoftAP mode (data path on the AP netif, IP 192.168.43.1)
 * @retval false STA mode (device joined an external AP)
 */
bool dashboard_wifi_is_ap_mode(void);

/* ----------------------------------------------------------------------------
 * Business request API: enqueue work to wifi task (non-blocking).
 * Return value only indicates enqueue success. Results logged via RTK_LOG.
 * --------------------------------------------------------------------------- */

/**
 * @brief Request OTA HTTP upgrade on wifi task.
 * @param host     IP string, NULL for default
 * @param port     port number
 * @param resource filename, NULL for default
 * @retval 0       enqueued
 * @retval <0      queue full / not initialized
 */
int dashboard_wifi_request_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_WIFI_H */
