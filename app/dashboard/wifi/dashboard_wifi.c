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
 * Dashboard WiFi dispatcher
 *
 *   SDK events        Shell/cmd handler
 *       |                    |
 *   on_join_status    cmd_dashboard_ota_http
 *       |                    |
 *       +----> g_wifi_msg_queue <----+
 *                    |
 *           dash_board_wifi_task (sole consumer, serial)
 *
 * We override __weak event_external_hdl[] in ameba_wificfg.c.
 * ============================================================================ */

#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"

#include "wifi_api.h"
#include "wifi_api_types.h"
#include "wifi_api_event.h"
#include "lwip_netconf.h"

#include "dashboard_wifi.h"
#include "dashboard_ota_http.h"

#define LOG_TAG "DASHBOARD-WIFI"

/* ---------------------------------------------------------------------------
 * Message types (type-tagged union)
 * ------------------------------------------------------------------------- */
typedef enum {
	DASHBOARD_WIFI_MSG_EVT_JOIN_STATUS,
	DASHBOARD_WIFI_MSG_EVT_DHCP_STATUS,
	DASHBOARD_WIFI_MSG_CMD_OTA_HTTP,
} dashboard_wifi_msg_type_t;

typedef struct {
	char host[64];
	u16  port;
	char resource[64];
} dashboard_wifi_ota_args_t;

typedef struct {
	dashboard_wifi_msg_type_t type;
	union {
		struct {
			u8  status;
			u8  channel;
			s8  rssi;
			u16 reason_or_status_code;
			s32 fail_reason;
			u16 disconn_reason;
		} join;
		struct {
			u8 dhcp_status;
		} dhcp;
		dashboard_wifi_ota_args_t ota;
	} u;
} dashboard_wifi_msg_t;

/* ---------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */
static rtos_queue_t  g_wifi_msg_queue = NULL;
static volatile bool g_wifi_online    = false;
static u8            g_join_state     = RTW_JOINSTATUS_UNKNOWN;

static int post_msg(const dashboard_wifi_msg_t *msg, uint32_t wait_ms)
{
	if (g_wifi_msg_queue == NULL) {
		return -1;
	}
	if (rtos_queue_send(g_wifi_msg_queue, (void *)msg, wait_ms) != RTK_SUCCESS) {
		return -2;
	}
	return 0;
}

int dashboard_wifi_request_ota_http(const char *host, u16 port, const char *resource)
{
	dashboard_wifi_msg_t msg = {0};
	msg.type = DASHBOARD_WIFI_MSG_CMD_OTA_HTTP;

	const char *h = host     ? host     : "";
	const char *r = resource ? resource : "";

	strncpy(msg.u.ota.host,     h, sizeof(msg.u.ota.host)     - 1);
	strncpy(msg.u.ota.resource, r, sizeof(msg.u.ota.resource) - 1);
	msg.u.ota.port = port;

	return post_msg(&msg, 100);
}

bool dashboard_wifi_is_online(void)
{
	return g_wifi_online;
}

/* ---------------------------------------------------------------------------
 * SDK event callbacks (run in SDK thread context, must not block)
 * ------------------------------------------------------------------------- */
static void on_join_status(u8 *evt_info)
{
	struct rtw_event_join_status_info *info = (struct rtw_event_join_status_info *)evt_info;
	dashboard_wifi_msg_t msg = {0};

	msg.type             = DASHBOARD_WIFI_MSG_EVT_JOIN_STATUS;
	msg.u.join.status    = info->status;
	msg.u.join.channel   = info->channel;
	msg.u.join.rssi      = info->rssi;

	switch (info->status) {
	case RTW_JOINSTATUS_FAIL:
		msg.u.join.fail_reason           = info->priv.fail.fail_reason;
		msg.u.join.reason_or_status_code = info->priv.fail.reason_or_status_code;
		break;
	case RTW_JOINSTATUS_DISCONNECT:
		msg.u.join.disconn_reason = info->priv.disconnect.disconn_reason;
		break;
	default:
		break;
	}

	(void)post_msg(&msg, 0);
}

static void on_dhcp_status(u8 *evt_info)
{
	struct rtw_event_dhcp_status *info = (struct rtw_event_dhcp_status *)evt_info;
	dashboard_wifi_msg_t msg = {0};

	msg.type              = DASHBOARD_WIFI_MSG_EVT_DHCP_STATUS;
	msg.u.dhcp.dhcp_status = info->dhcp_status;

	(void)post_msg(&msg, 0);
}

/* Strong symbol overrides weak default in ameba_wificfg.c */
struct rtw_event_hdl_func_t event_external_hdl[2] = {
	{RTW_EVENT_JOIN_STATUS, on_join_status},
	{RTW_EVENT_DHCP_STATUS, on_dhcp_status},
};
u16 array_len_of_event_external_hdl =
	sizeof(event_external_hdl) / sizeof(struct rtw_event_hdl_func_t);

/* ---------------------------------------------------------------------------
 * Dispatcher internal: event handlers
 * ------------------------------------------------------------------------- */

static void handle_join_status(const dashboard_wifi_msg_t *msg)
{
	g_join_state = msg->u.join.status;

	switch (msg->u.join.status) {
	case RTW_JOINSTATUS_SUCCESS:
		RTK_LOGI(LOG_TAG, "Join SUCCESS, ch=%u rssi=%d (waiting DHCP...)\n",
				 msg->u.join.channel, msg->u.join.rssi);
		break;
	case RTW_JOINSTATUS_FAIL:
		g_wifi_online = false;
		RTK_LOGE(LOG_TAG, "Join FAIL, reason=%d code=%u\n",
				 (int)msg->u.join.fail_reason, msg->u.join.reason_or_status_code);
		break;
	case RTW_JOINSTATUS_DISCONNECT:
		g_wifi_online = false;
		RTK_LOGW(LOG_TAG, "DISCONNECTED, reason=%u\n", msg->u.join.disconn_reason);
		break;
	default:
		break;
	}
}

static void handle_dhcp_status(const dashboard_wifi_msg_t *msg)
{
	switch (msg->u.dhcp.dhcp_status) {
	case DHCP_ADDRESS_ASSIGNED:
		g_wifi_online = true;
		RTK_LOGI(LOG_TAG, "DHCP ASSIGNED, WiFi ONLINE. type cmd to use it.\n");
		break;
	case DHCP_STOP:
	case DHCP_TIMEOUT:
		g_wifi_online = false;
		RTK_LOGW(LOG_TAG, "DHCP %s\n",
				 msg->u.dhcp.dhcp_status == DHCP_STOP ? "STOPPED" : "TIMEOUT");
		break;
	default:
		break;
	}
}

/* ---------------------------------------------------------------------------
 * Dispatcher main loop (single consumer, sequential)
 * ------------------------------------------------------------------------- */
static void dispatch_msg(const dashboard_wifi_msg_t *msg)
{
	switch (msg->type) {
	case DASHBOARD_WIFI_MSG_EVT_JOIN_STATUS:
		handle_join_status(msg);
		break;
	case DASHBOARD_WIFI_MSG_EVT_DHCP_STATUS:
		handle_dhcp_status(msg);
		break;
	case DASHBOARD_WIFI_MSG_CMD_OTA_HTTP:
		(void)run_ota_http(msg->u.ota.host, msg->u.ota.port, msg->u.ota.resource);
		break;
	default:
		RTK_LOGE(LOG_TAG, "unknown msg type %d\n", (int)msg->type);
		break;
	}
}

void dash_board_wifi_task(void *param)
{
	UNUSED(param);

	if (rtos_queue_create(&g_wifi_msg_queue, 16, sizeof(dashboard_wifi_msg_t)) != RTK_SUCCESS) {
		RTK_LOGE(LOG_TAG, "create wifi msg queue failed\n");
		rtos_task_delete(NULL);
		return;
	}

	while (!(wifi_is_running(STA_WLAN_INDEX) || wifi_is_running(SOFTAP_WLAN_INDEX))) {
		rtos_time_delay_ms(500);
	}

	while (1) {
		dashboard_wifi_msg_t msg;
		if (rtos_queue_receive(g_wifi_msg_queue, &msg, 0xFFFFFFFF) == RTK_SUCCESS) {
			dispatch_msg(&msg);
		}
	}
}
