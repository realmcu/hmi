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
 * Dashboard OTA HTTP module (business plugin on top of dashboard_wifi dispatcher)
 *
 * This file **only** does two things:
 *   1) Provides run_ota_http() -- the blocking download/flash flow, called
 *      by the WiFi dispatcher in its own task context.
 *   2) Provides shell command "ota_http" -- handler just encapsulates the
 *      request and calls dashboard_wifi_request_ota_http(), returns immediately.
 *
 * Simplifications vs earlier versions:
 *   - No longer spawns its own task: runs on the dispatcher task.
 *   - No g_ota_busy guard: dispatcher queue is single-consumer, naturally serial.
 *   - No ota_http_args_t struct across files: plain (host, port, resource) params.
 *
 * Reference: example/ota/ota_http/example_ota_http.c
 * ============================================================================ */

#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "section_config.h"

/* Defensive double-check: dispatcher checks online on enqueue, but connection may
 * briefly drop before socket creation; re-check inside run_ota_http. */
#include "lwip_netconf.h"

#include "ota_api.h"

#include "dashboard_wifi.h"
#include "dashboard_ota_http.h"

#define LOG_TAG "DASHBOARD-OTA"

extern void sys_reset(void);

/* ============================================================================
 * run_ota_http -- the actual blocking function (tens of seconds)
 *
 * Calling convention: runs in dashboard_wifi dispatcher task context. During
 * OTA, other queue messages are queued and processed after OTA completes.
 * This is the desired "serial" semantics.
 *
 * Flow (see example_ota_http.c::ota_task()):
 *   1) Double-check IP online -> abort if not
 *   2) (TrustZone only) secure context
 *   3) Alloc ota_context_t, ota_init, ota_start
 *   4) Success -> sys_reset into new firmware
 *   5) Cleanup ctx, return
 *
 * @return 0 success (usually doesn't return due to reset), <0 failure
 * ============================================================================ */
int run_ota_http(const char *host, u16 port, const char *resource)
{
	ota_context_t *ctx = NULL;
	int            ret = -1;

	/* /NULL */
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

	/* const → const SDK const */
	ret = ota_init(ctx, (char *)host, port, (char *)resource, OTA_HTTP);
	if (ret != OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ota_init failed\n");
		goto cleanup;
	}

	ret = ota_start(ctx);

	if (ret == OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] success, rebooting...\n");
		rtos_time_delay_ms(20);  /* UART log */
		sys_reset();
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
 *   argc = number of user arguments (excluding command name)
 *   argv[0..argc-1] = argument strings
 *
 * This version parses args -> calls dashboard_wifi_request_ota_http() -> returns immediately.
 * Actual download runs on the dispatcher task, shell is not blocked.
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

	/* Check online status upfront (from dashboard_wifi status).
	 * Reject offline immediately to give the user direct feedback. */
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

	/* /argv */
	if (dashboard_wifi_request_ota_http(host, port, resource) != 0) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] enqueue request failed\n");
		return FALSE;
	}

	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] queued\n");
	return TRUE;
}

/* CMD_TABLE_DATA_SECTION .cmd.table.data shell
 * */
CMD_TABLE_DATA_SECTION
const COMMAND_TABLE dashboard_ota_http_cmd_table[] = {
	{"ota_http", cmd_dashboard_ota_http},
};
