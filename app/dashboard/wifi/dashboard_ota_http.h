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
 * Dashboard OTA HTTP module public interface
 *
 * A business plugin on top of the dashboard_wifi dispatcher. Exposes only:
 *   - Default parameter macros (DASHBOARD_OTA_HTTP_DEFAULT_*)
 *   - run_ota_http() -- called by dispatcher, **do not** call directly
 *
 * Shell command "ota_http" auto-registers via CMD_TABLE_DATA_SECTION in .c file.
 *
 * Usage:
 *   - User types "ota_http [host] [port] [resource]" on serial console
 *   - cmd handler calls dashboard_wifi_request_ota_http(...)
 *   - Dispatcher calls run_ota_http() in its task context
 *
 * Defaults:
 *   HOST     -- HTTP server IP. Preset to PC WLAN adapter IP.
 *   PORT     -- Realtek DownloadServer(HTTP) default 8082.
 *   RESOURCE -- Firmware name, matches ota_all.bin.
 *
 * Shell overrides:
 *   ota_http                              all defaults
 *   ota_http 192.168.1.100               override host only
 *   ota_http 192.168.1.100 8080          override host + port
 *   ota_http 192.168.1.100 8080 a.bin    override all
 *   ota_http ?                            print usage
 * ============================================================================ */

#include "basic_types.h"   /* u16 */

#ifdef __cplusplus
extern "C" {
#endif

#define DASHBOARD_OTA_HTTP_DEFAULT_HOST     "192.168.51.162"
#define DASHBOARD_OTA_HTTP_DEFAULT_PORT     8082
#define DASHBOARD_OTA_HTTP_DEFAULT_RESOURCE "ota_all.bin"

/**
 * @brief Blocking OTA HTTP upgrade function.
 *
 * **Should only be called by dashboard_wifi dispatcher**. Normal callers should
 * use dashboard_wifi_request_ota_http() to enqueue async. Calling this directly
 * blocks the caller for tens of seconds (HTTP download + flash write).
 *
 * NULL/empty/0 params fall back to DASHBOARD_OTA_HTTP_DEFAULT_*.
 *
 * @param host      HTTP server address (IP string)
 * @param port      port number
 * @param resource  HTTP resource path
 * @retval 0    success (note: function usually does not return due to sys_reset)
 * @retval <0   failure (IP not ready / malloc failed / ota_start failed)
 */
int run_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_OTA_HTTP_H */
