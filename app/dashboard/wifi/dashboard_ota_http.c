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
 * Dashboard OTA HTTP (dispatcher plugin)
 * Provides run_ota_http() + shell command "ota_http".
 * Runs on dispatcher task (blocking ~30s for download+flash).
 * ============================================================================ */

#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "section_config.h"

#include "lwip_netconf.h"

#include "ota_api.h"

#include "dashboard_wifi.h"
#include "dashboard_ota_http.h"

extern void sys_reset(void);

int run_ota_http(const char *host, u16 port, const char *resource)
{
	ota_context_t *ctx = NULL;
	int            ret = -1;

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

	ret = ota_init(ctx, (char *)host, port, (char *)resource, OTA_HTTP);
	if (ret != OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ota_init failed\n");
		goto cleanup;
	}

	ret = ota_start(ctx);

	if (ret == OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] success, rebooting...\n");
		rtos_time_delay_ms(20);
		sys_reset();
	} else {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ota_start failed: %d\n", ret);
	}

cleanup:
	ota_deinit(ctx);
	rtos_mem_free(ctx);
	return (ret == OTA_OK) ? 0 : -3;
}

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

	if (dashboard_wifi_request_ota_http(host, port, resource) != 0) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] enqueue request failed\n");
		return FALSE;
	}

	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] queued\n");
	return TRUE;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE dashboard_ota_http_cmd_table[] = {
	{"ota_http", cmd_dashboard_ota_http},
};
