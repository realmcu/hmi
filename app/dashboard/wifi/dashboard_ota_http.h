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
 * Public interface for the Dashboard OTA HTTP module.
 *
 * This is a service plugin on top of the dashboard_wifi dispatcher. It exposes
 * only:
 *   - Default parameter macros (DASHBOARD_OTA_HTTP_DEFAULT_*).
 *   - run_ota_http(), called by the dispatcher only. Do not call it directly
 *     from other code.
 *
 * The "ota_http" shell command self-registers in the .c file through
 * CMD_TABLE_DATA_SECTION. The linker collects it automatically, so users of
 * this header do not need to see it.
 *
 * Usage:
 *   - The user enters "ota_http [host] [port] [resource]" on the serial shell.
 *   - The command handler calls dashboard_wifi_request_ota_http(...) to enqueue
 *     the request into the WiFi dispatcher queue.
 *   - The dispatcher calls run_ota_http() in its own task context, where the
 *     service actually runs.
 *
 * Default parameters, shared with dashboard_ota_http.c:
 *   HOST     - HTTP server IP, preset to the PC WLAN adapter IP.
 *   PORT     - Realtek DownloadServer (HTTP) listens on 8082 by default.
 *   RESOURCE - Firmware name served by the server, matching built ota_all.bin.
 *
 * Users can override all defaults from the shell:
 *   ota_http                              use all defaults
 *   ota_http 192.168.1.100                override host only
 *   ota_http 192.168.1.100 8080           override host and port
 *   ota_http 192.168.1.100 8080 a.bin     override all parameters
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
 * @brief Blocking function that performs one OTA HTTP upgrade.
 *
 * This should only be called by the dashboard_wifi dispatcher. Normal callers
 * should use dashboard_wifi_request_ota_http() to enqueue the request
 * asynchronously. Calling this function directly blocks the caller task for
 * tens of seconds while HTTP download and flash writing run.
 *
 * Passing NULL, an empty string, or 0 for any parameter falls back to the
 * corresponding DASHBOARD_OTA_HTTP_DEFAULT_* value.
 *
 * @param host      HTTP server address as an IP string.
 * @param port      Port.
 * @param resource  HTTP resource path.
 * @retval 0    Success. The function usually does not return on success
 *              because it calls sys_reset internally.
 * @retval <0   Failure: IP not ready, malloc failed, or ota_start failed.
 */
int run_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_OTA_HTTP_H */
