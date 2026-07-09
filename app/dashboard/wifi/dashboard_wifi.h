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
 * Dashboard WiFi subsystem: unified dispatcher mode.
 *
 * Role, parallel to ble/dashboard_ble.h:
 *   - One long-lived thread, dash_board_wifi_task, acts as the WiFi worker.
 *   - All work entering this thread goes through one shared queue:
 *       * WiFi events posted by the SDK (join_status / dhcp_status) are
 *         enqueued by internal callbacks.
 *       * User shell commands (ota_http / future stream_img, etc.) are enqueued
 *         by command handlers.
 *   - The thread consumes the queue in order and is naturally serialized: OTA
 *     is not interrupted by new commands; new commands wait in the queue. This
 *     avoids the complexity of multiple tasks racing for sockets or flash.
 *
 * Compared with the BLE task:
 *   The task in dashboard_ble.c is also a queue consumer, but its input is only
 *   key events. This WiFi task has richer input (events + commands), so it uses
 *   a type-tagged union, dashboard_wifi_msg_t in the .c file, to distinguish
 *   message kinds.
 *
 * How service modules plug in:
 *   1) A service module, such as dashboard_ota_http.c, implements a
 *      run_xxx(args) function that performs the real blocking work in the WiFi
 *      task context.
 *   2) The service module's shell command handler calls
 *      dashboard_wifi_request_xxx(...) to enqueue the request into the WiFi
 *      task queue.
 *   3) The WiFi task dispatch logic calls run_xxx according to msg.type.
 *
 * Adding a feature then touches only two places: the service file, plus one
 * case branch in dashboard_wifi.c.
 * ============================================================================ */

#include <stdbool.h>
#include "basic_types.h"   /* u16 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi dispatcher thread entry.
 *
 * Started by app_example() through rtos_task_create during boot. Inside the
 * task:
 *   1) Create the message queue, before event callbacks can be registered.
 *   2) Wait until the WiFi subsystem is running, checked by wifi_is_running.
 *   3) Enter while(1), block on the queue for dashboard_wifi_msg_t, and
 *      dispatch according to type.
 *
 * Never returns. A 4 KB stack is recommended to support blocking services such
 * as OTA.
 *
 * @param param  Unused. Pass NULL.
 */
void dash_board_wifi_task(void *param);

/**
 * @brief Query whether WiFi/IP is currently available.
 *
 * Maintained by the WiFi task when it receives events such as
 * DHCP_ADDRESS_ASSIGNED or DISCONNECT. The only writer is the WiFi task itself,
 * which writes serially in one thread; readers may be any task, such as command
 * handlers. This is a classic single-writer / multi-reader case, where volatile
 * is enough for visibility.
 *
 * Note: this returns the most recently observed state and cannot immediately
 * reflect a transient disconnect. Critical paths, such as before OTA opens the
 * socket, should call LwIP_Check_Connectivity for a second check.
 *
 * @retval true   STA is associated to the AP and LwIP has an IP address.
 * @retval false  Any other state.
 */
bool dashboard_wifi_is_online(void);

/* ----------------------------------------------------------------------------
 * Service request APIs: enqueue work requests into the WiFi task queue.
 *
 * These functions return immediately without blocking. Actual work runs
 * asynchronously on the WiFi task. Their return values only indicate whether
 * enqueueing succeeded, not the final service result. Service results are
 * reported to the serial port through RTK_LOG.
 *
 * To add a service:
 *   - Add a dashboard_wifi_request_<feature>() declaration here.
 *   - Add the corresponding case in the dispatch switch in dashboard_wifi.c.
 *   - Implement the blocking run_<feature>(args) function in the new service .c file.
 * --------------------------------------------------------------------------- */

/**
 * @brief Request the WiFi task to run one OTA HTTP upgrade in its context.
 *
 * The caller, usually a command handler, provides the server address, port, and
 * resource name. This function copies those strings into the queue message, so
 * the queued copy remains valid even after caller stack strings go out of scope.
 *
 * @param host     HTTP server host as an IP string. Pass NULL to use default.
 * @param port     Port.
 * @param resource Resource path or file name. Pass NULL to use default.
 * @retval 0       Enqueue succeeded; the service runs asynchronously later.
 * @retval <0      Enqueue failed because the queue is full or uninitialized.
 */
int dashboard_wifi_request_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_WIFI_H */
