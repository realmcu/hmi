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

#ifndef DASHBOARD_OTA_HTTP_H
#define DASHBOARD_OTA_HTTP_H

/* ============================================================================
 * Dashboard OTA HTTP (dispatcher plugin)
 * cmd: ota_http [host] [port] [resource]
 * Use dashboard_wifi_request_ota_http() to enqueue async.
 * ============================================================================ */

#include "basic_types.h"   /* u16 */

#ifdef __cplusplus
extern "C" {
#endif

#define DASHBOARD_OTA_HTTP_DEFAULT_HOST     "192.168.43.100"
#define DASHBOARD_OTA_HTTP_DEFAULT_PORT     8082
#define DASHBOARD_OTA_HTTP_DEFAULT_RESOURCE "ota_all.bin"

/**
 * @brief Blocking OTA HTTP upgrade.
 * Call via dashboard_wifi_request_ota_http() (async). Direct call blocks ~30s.
 * NULL/0 params fall back to DASHBOARD_OTA_HTTP_DEFAULT_*.
 * @retval 0  success (usually calls sys_reset)
 * @retval <0 failure
 */
int run_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_OTA_HTTP_H */
