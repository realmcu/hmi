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
 * Dashboard WiFi subsystem -- unified dispatcher pattern
 *
 * Role (parallel to ble/dashboard_ble.h):
 *   - A persistent thread dash_board_wifi_task acts as "WiFi worker".
 *   - All work enters via a shared queue:
 *       * SDK WiFi events (join_status / dhcp_status) -> internal callback enqueue
 *       * Shell commands (ota_http / future stream_img) -> cmd handler enqueue
 *   - Thread consumes queue sequentially, **naturally serial**: OTA runs
 *     without interruption; new commands queue up. No multi-task socket/flash race.
 *
 * Adding a new feature:
 *   1) Write a run_xxx(args) function in the feature module (runs in wifi task context).
 *   2) Shell cmd handler calls dashboard_wifi_request_xxx() to enqueue.
 *   3) wifi task dispatches by msg.type -> calls run_xxx.
 *
 * Only two places to change: feature file + one case in dashboard_wifi.c.
 * ============================================================================ */

#include <stdbool.h>
#include "basic_types.h"   /* u16 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi dispatcher thread entry.
 *
 * Created by app_example() via rtos_task_create at startup. Internally:
 *   1) Create message queue (must precede event callback registration)
 *   2) Wait for WiFi subsystem to start (wifi_is_running)
 *   3) Enter while(1), blocking-receive dashboard_wifi_msg_t, dispatch by type
 *
 * Never returns. Recommended stack: 4KB (to support blocking OTA operations).
 *
 * @param param  unused, pass NULL.
 */
void dash_board_wifi_task(void *param);

/**
 * @brief Check if WiFi/IP is currently available.
 *
 * Maintained by wifi task on DHCP_ADDRESS_ASSIGNED / DISCONNECT events.
 * Single writer (wifi task) / multi-reader (any task) -- volatile is sufficient.
 *
 * Note: returns **last observed** state, may not reflect instant disconnection.
 * On critical paths (e.g., before OTA socket), call LwIP_Check_Connectivity
 * for a double-check.
 *
 * @retval true   STA associated with AP and LwIP has an IP
 * @retval false  otherwise
 */
bool dashboard_wifi_is_online(void);

/* ----------------------------------------------------------------------------
 * Business request API: enqueue work requests to the wifi task's queue.
 *
 * These functions return immediately (non-blocking). Actual work runs
 * asynchronously on the wifi task. Return value only indicates whether
 * the enqueue succeeded. Business results are logged via RTK_LOG.
 *
 * Adding a new feature:
 *   - Add dashboard_wifi_request_<feature>() declaration here
 *   - Add case in dashboard_wifi.c dispatch switch
 *   - Implement run_<feature>(args) blocking function in feature .c
 * --------------------------------------------------------------------------- */

/**
 * @brief Request wifi task to run an OTA HTTP upgrade in its context.
 *
 * Caller (typically cmd handler) provides server address/port/resource.
 * The function copies them into the queue message, so they remain valid
 * even if the caller's stack strings become invalid.
 *
 * @param host     HTTP server host (IP string), NULL for default
 * @param port     port number
 * @param resource resource path (filename), NULL for default
 * @retval 0       enqueue success (async execution follows)
 * @retval <0      enqueue failed (queue full / not initialized)
 */
int dashboard_wifi_request_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_WIFI_H */
