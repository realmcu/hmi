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

/* ============================================================================
 * Dashboard OTA HTTP service module, implemented as a service plugin on top
 * of the dashboard_wifi dispatcher.
 *
 * This file does exactly two things:
 *   1) Provides run_ota_http(), the blocking download/flash workflow called
 *      by the WiFi dispatcher in its own task context.
 *   2) Provides the "ota_http" shell command. The handler only packages the
 *      request and submits it to dashboard_wifi_request_ota_http(), then
 *      returns immediately without blocking the shell.
 *
 * Simplifications compared with the previous version:
 *   - No private task is spawned; the service runs on the dispatcher task,
 *     saving one stack allocation.
 *   - No g_ota_busy single-instance guard is needed; the dispatcher queue has
 *     a single consumer, so requests are naturally serialized.
 *   - No ota_http_args_t is shared across files; the module boundary is kept
 *     minimal with plain (host, port, resource) arguments.
 *
 * Reference: example/ota/ota_http/example_ota_http.c. The workflow is almost
 * identical; only the entry point differs (the example uses ota_task, while
 * this module is called synchronously through run_ota_http).
 * ============================================================================ */

#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "section_config.h"

/* Used for a defensive second check: the dispatcher verifies online state when
 * queuing the request, but the link may drop before the socket is opened.
 * run_ota_http checks connectivity again after it starts. */
#include "lwip_netconf.h"

#include "ota_api.h"

#include "dashboard_wifi.h"
#include "dashboard_ota_http.h"

#define LOG_TAG "DASHBOARD-OTA"

extern void sys_reset(void);

/* ============================================================================
 * run_ota_http - the function that does the real work. It blocks for tens of
 * seconds depending on download and flash time.
 *
 * Calling convention: execute from the dashboard_wifi dispatcher task context.
 * While this function runs, that task does not handle other queued messages,
 * so WiFi events raised during OTA remain queued until OTA finishes. This is
 * the intended serialized behavior.
 *
 * Workflow, matching example_ota_http.c::ota_task():
 *   1) Re-check that IP connectivity is online, otherwise abort.
 *   2) Create the secure context when TrustZone mode requires it.
 *   3) Allocate ota_context_t, then call ota_init and ota_start.
 *   4) On success, call sys_reset to boot the new firmware.
 *   5) Clean up ctx and return.
 *
 * @return 0 on success (normally this does not return because success resets
 *         the system), <0 on failure.
 * ============================================================================ */
int run_ota_http(const char *host, u16 port, const char *resource)
{
	ota_context_t *ctx = NULL;
	int            ret = -1;

	/* Treat NULL or empty strings as defaults to keep caller logic simple. */
	if (host == NULL || host[0] == '\0') {
		host = DASHBOARD_OTA_HTTP_DEFAULT_HOST;
	}
	if (resource == NULL || resource[0] == '\0') {
		resource = DASHBOARD_OTA_HTTP_DEFAULT_RESOURCE;
	}
	if (port == 0) {
		port = DASHBOARD_OTA_HTTP_DEFAULT_PORT;
	}

	if (LwIP_Check_Connectivity(NETIF_WLAN_STA_INDEX) != CONNECTION_VALID) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] WiFi/IP not ready, abort\n");
		return -1;
	}

#if defined(configENABLE_TRUSTZONE) && (configENABLE_TRUSTZONE == 1)
	rtos_create_secure_context(configMINIMAL_SECURE_STACK_SIZE);
#endif

	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
			 "\n\r<<<<<< Dashboard OTA HTTP: %s:%u/%s >>>>>>\n\r",
			 host, port, resource);

	ctx = (ota_context_t *)rtos_mem_malloc(sizeof(ota_context_t));
	if (ctx == NULL) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ctx malloc failed\n");
		return -2;
	}
	memset(ctx, 0, sizeof(ota_context_t));

	/* Cast away const because the lower SDK API is not const-correct; it does not write. */
	ret = ota_init(ctx, (char *)host, port, (char *)resource, OTA_HTTP);
	if (ret != OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ota_init failed\n");
		goto cleanup;
	}

	ret = ota_start(ctx);

	if (ret == OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] success, rebooting...\n");
		rtos_time_delay_ms(20);  /* Give UART time to flush the log. */
		sys_reset();
		/* Does not return. */
	} else {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ota_start failed: %d\n", ret);
	}

cleanup:
	ota_deinit(ctx);
	rtos_mem_free(ctx);
	return (ret == OTA_OK) ? 0 : -3;
}

/* ============================================================================
 * Shell command handler
 *
 * Signature convention: u32 func(u16 argc, u8 *argv[])
 *   argc = number of user-provided arguments, excluding the command name.
 *   argv[0..argc-1] = argument strings.
 *
 * This version only parses arguments, calls dashboard_wifi_request_ota_http(),
 * and returns immediately. The actual download runs on the dispatcher task, so
 * the shell is not blocked.
 * ============================================================================ */
static void usage(void)
{
	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
			 "Usage: ota_http [host] [port] [resource]\n"
			 "  defaults: %s %u %s\n",
			 DASHBOARD_OTA_HTTP_DEFAULT_HOST,
			 DASHBOARD_OTA_HTTP_DEFAULT_PORT,
			 DASHBOARD_OTA_HTTP_DEFAULT_RESOURCE);
}

static u32 cmd_dashboard_ota_http(u16 argc, u8 *argv[])
{
	const char *host     = NULL;
	u16         port     = 0;
	const char *resource = NULL;

	if (argc >= 1 && argv[0] && (strcmp((const char *)argv[0], "?") == 0 ||
								 strcmp((const char *)argv[0], "help") == 0)) {
		usage();
		return TRUE;
	}

	/* Check online state early using dashboard_wifi state. Reject offline requests
	 * before enqueueing so the user gets a direct message instead of a later fail. */
	if (!dashboard_wifi_is_online()) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
				 "[OTA] WiFi not online yet, please connect first.\n");
		return TRUE;
	}

	if (argc >= 1 && argv[0]) {
		host = (const char *)argv[0];
	}
	if (argc >= 2 && argv[1]) {
		port = (u16)atoi((const char *)argv[1]);
	}
	if (argc >= 3 && argv[2]) {
		resource = (const char *)argv[2];
	}

	/* The dispatcher copies strings into the queue message, so stack or argv pointers are safe here. */
	if (dashboard_wifi_request_ota_http(host, port, resource) != 0) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] enqueue request failed\n");
		return FALSE;
	}

	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] queued\n");
	return TRUE;
}

/* CMD_TABLE_DATA_SECTION places the command table in .cmd.table.data. The shell
 * scans that section at startup and collects every command it finds. Each
 * module registers its own commands without depending on other modules. */
CMD_TABLE_DATA_SECTION
const COMMAND_TABLE dashboard_ota_http_cmd_table[] = {
	{"ota_http", cmd_dashboard_ota_http},
};
