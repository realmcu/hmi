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
 * Dashboard WiFi 派发器（dispatcher）实现
 *
 * 整体架构（详见 dashboard_wifi.h 顶部的说明）：
 *
 *   ┌──────────────────────┐                  ┌──────────────────────┐
 *   │ SDK 内部 WiFi 任务   │                  │ shell 任务           │
 *   │ (driver / 协议栈)    │                  │ (用户输入 cmd)       │
 *   └──────────┬───────────┘                  └──────────┬───────────┘
 *              │ 调用 event_external_hdl[]                │ 调用 cmd handler
 *              │                                          │
 *              ▼                                          ▼
 *   on_join_status / on_dhcp_status            cmd_dashboard_ota_http
 *              │                                          │
 *              │ 构造 dashboard_wifi_msg_t                │ 构造 dashboard_wifi_msg_t
 *              │ rtos_queue_send                          │ rtos_queue_send
 *              │                                          │
 *              └─────────────┬────────────────────────────┘
 *                            ▼
 *                  ┌────────────────────┐
 *                  │ g_wifi_msg_queue   │ ← 同一条队列
 *                  └─────────┬──────────┘
 *                            │ rtos_queue_receive
 *                            ▼
 *                ┌─────────────────────────┐
 *                │ dash_board_wifi_task    │ ← 唯一消费者，单线程顺序
 *                │   switch (msg.type)     │
 *                └─────────────────────────┘
 *
 * --- 关于 SDK 自身的 WiFi 事件回调 -----------------------------------------
 * 我们覆盖的是 component/soc/usrcfg/amebagreen2/ameba_wificfg.c 里的
 * `__weak event_external_hdl[]` —— SDK 留给应用层的钩子。SDK 内部的认证、
 * 关联、DHCP、auto-reconnect 等业务逻辑，走的是 wifi_event_handle_internal /
 * wifi_event_handle_common 等独立路径，**不受影响**。我们这里只是订阅通知。
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
/* 派发器需要看到各个业务插件的 run_xxx() 接口。
 * 加新业务时在这里追加 #include "dashboard_<feature>.h"。 */
#include "dashboard_ota_http.h"

#define LOG_TAG "DASHBOARD-WIFI"

/* ============================================================================
 * 消息定义（仅在本文件可见，不暴露给其他模块）
 *
 * 用 type-tagged union 区分消息种类。新加业务只要：
 *   1) 加一个 MSG_CMD_xxx 枚举值
 *   2) 加一个 union 成员承载该业务的参数
 *   3) 在 .h 里加一个 dashboard_wifi_request_xxx() wrapper
 *   4) 在 dispatch_msg() 里加一个 case
 * ============================================================================ */
typedef enum {
	/* 来自 SDK 的事件（异步推上来） */
	DASHBOARD_WIFI_MSG_EVT_JOIN_STATUS,
	DASHBOARD_WIFI_MSG_EVT_DHCP_STATUS,

	/* 来自用户的命令（shell / GUI 触发） */
	DASHBOARD_WIFI_MSG_CMD_OTA_HTTP,
	/* 未来扩展占位：
	 * DASHBOARD_WIFI_MSG_CMD_STREAM_IMG,
	 * DASHBOARD_WIFI_MSG_CMD_FILE_DOWNLOAD,
	 * ...
	 */
} dashboard_wifi_msg_type_t;

/* OTA 业务的参数（必须可被值拷贝到队列消息里）。
 * 这个结构体也被 dashboard_ota_http.c 里的 run_ota_http() 消费。 */
typedef struct {
	char host[64];
	u16  port;
	char resource[64];
} dashboard_wifi_ota_args_t;

typedef struct {
	dashboard_wifi_msg_type_t type;
	union {
		struct {
			u8  status;                 /* enum rtw_join_status */
			u8  channel;
			s8  rssi;
			u16 reason_or_status_code;
			s32 fail_reason;
			u16 disconn_reason;
		} join;
		struct {
			u8 dhcp_status;             /* DHCP_ADDRESS_ASSIGNED / DHCP_STOP / DHCP_TIMEOUT */
		} dhcp;
		dashboard_wifi_ota_args_t ota;
	} u;
} dashboard_wifi_msg_t;

/* ============================================================================
 * 模块状态
 * ============================================================================ */
static rtos_queue_t  g_wifi_msg_queue = NULL;
static volatile bool g_wifi_online    = false;  /* 单写者（task）+ 多读者，volatile 即可 */
static u8            g_join_state     = RTW_JOINSTATUS_UNKNOWN;

/* run_ota_http() 在 dashboard_ota_http.h 里声明，用最朴素的参数列表
 * （不依赖任何业务专属 struct），派发器只把 union 里的字段解包传出去。
 * 加新业务时同样原则：每个业务一个 run_xxx(基本类型...)。 */

/* ============================================================================
 * 公共 API：投递消息（给 cmd handler 用）
 *
 * 注意 wait_ms：
 *   - 0      = 不等队列空位，满了立即失败（适合事件回调，不能阻塞 SDK）
 *   - 100ms  = 等一小会（适合 cmd handler，给业务 task 一点时间消费旧消息）
 *   - 0xFFFFFFFF = 一直等（不推荐，shell 会卡）
 * ============================================================================ */
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

	const char *h = host     ? host     : "";  /* 空串让 run_ota_http 用默认值 */
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

/* ============================================================================
 * SDK 事件回调
 *
 * 重要：这些函数运行在 SDK 内部线程上下文，**不能阻塞**。只做"打包字段 +
 * 入队"，全部细致处理留给派发器线程。入队用 0 超时，满了就丢弃事件并打 warn。
 * ============================================================================ */
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

/* ----------------------------------------------------------------------------
 * event_external_hdl[] 强符号定义。
 *
 * SDK 在 ameba_wificfg.c 里给了 __weak 默认值（一个无意义占位）。我们这里
 * 提供同名强符号，链接器选我们这份。SDK 收到事件后会遍历这个表分发到我们
 * 的 handler。
 *
 * 加新事件订阅：在数组里追加 {EVT_ID, your_callback}，并把 array_len 一起改。
 * 比如想监听 SoftAP 模式下 STA 关联：{RTW_EVENT_AP_STA_ASSOC, on_ap_sta_assoc}
 * ---------------------------------------------------------------------------- */
struct rtw_event_hdl_func_t event_external_hdl[2] = {
	{RTW_EVENT_JOIN_STATUS, on_join_status},
	{RTW_EVENT_DHCP_STATUS, on_dhcp_status},
};
u16 array_len_of_event_external_hdl =
	sizeof(event_external_hdl) / sizeof(struct rtw_event_hdl_func_t);

/* ============================================================================
 * 派发器内部：事件处理
 * ============================================================================ */
static const char *join_status_str(u8 s)
{
	switch (s) {
	case RTW_JOINSTATUS_STARTING:           return "STARTING";
	case RTW_JOINSTATUS_SCANNING:           return "SCANNING";
	case RTW_JOINSTATUS_AUTHENTICATING:     return "AUTHENTICATING";
	case RTW_JOINSTATUS_AUTHENTICATED:      return "AUTHENTICATED";
	case RTW_JOINSTATUS_ASSOCIATING:        return "ASSOCIATING";
	case RTW_JOINSTATUS_ASSOCIATED:         return "ASSOCIATED";
	case RTW_JOINSTATUS_4WAY_HANDSHAKING:   return "4WAY_HANDSHAKING";
	case RTW_JOINSTATUS_4WAY_HANDSHAKE_DONE:return "4WAY_DONE";
	case RTW_JOINSTATUS_SUCCESS:            return "SUCCESS";
	case RTW_JOINSTATUS_FAIL:               return "FAIL";
	case RTW_JOINSTATUS_DISCONNECT:         return "DISCONNECT";
	default:                                return "UNKNOWN";
	}
}

static void handle_join_status(const dashboard_wifi_msg_t *msg)
{
	g_join_state = msg->u.join.status;

	switch (msg->u.join.status) {
	case RTW_JOINSTATUS_SUCCESS:
		RTK_LOGI(LOG_TAG, "Join SUCCESS, ch=%u rssi=%d (waiting DHCP...)\n",
				 msg->u.join.channel, msg->u.join.rssi);
		/* 故意不在这里设 online。等 DHCP_ADDRESS_ASSIGNED 事件再设。 */
		break;
	case RTW_JOINSTATUS_FAIL:
		g_wifi_online = false;
		RTK_LOGE(LOG_TAG, "Join FAIL, reason=%d code=%u\n",
				 (int)msg->u.join.fail_reason, msg->u.join.reason_or_status_code);
		break;
	case RTW_JOINSTATUS_DISCONNECT:
		g_wifi_online = false;
		RTK_LOGW(LOG_TAG, "DISCONNECTED, reason=%u\n", msg->u.join.disconn_reason);
		/* SDK 默认 auto-reconnect 会自己尝试重连；想自定义就在这里 hook。 */
		break;
	default:
		RTK_LOGD(LOG_TAG, "Join state: %s\n", join_status_str(msg->u.join.status));
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

/* ============================================================================
 * 派发器主循环
 *
 * 单消费者顺序处理。事件类的 handle_xxx 通常很快（更新标志 + 打 log）；
 * 命令类的 run_xxx 可能阻塞很久（OTA 30s+，未来图片流 60s）—— 这是 by
 * design，期间新消息会在 queue 里排队，OTA 跑完再处理，无需互斥。
 *
 * 队列容量在 task 启动时设定（默认 16）：
 *   - 一次入网会产生 ~7 个 join 状态变化
 *   - DHCP 1~2 个事件
 *   - 加上零散的命令，16 足够容纳一次握手 + 几条 cmd
 * ============================================================================ */
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
		/* 进入这里时如果 WiFi 不在线，run_ota_http 内部会自己 abort 并打 log。
		 * 解包 union 里的 OTA 参数传给业务函数。 */
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

	/* 必须在第一个 WiFi 事件可能到来之前完成 queue 创建。app_example()
	 * 拉起这个 task 的时机已经足够早（在 wifi_init 之前）。 */
	if (rtos_queue_create(&g_wifi_msg_queue, 16, sizeof(dashboard_wifi_msg_t)) != RTK_SUCCESS) {
		RTK_LOGE(LOG_TAG, "create wifi msg queue failed\n");
		rtos_task_delete(NULL);
		return;
	}

	RTK_LOGI(LOG_TAG, "wifi_task: waiting for WiFi subsystem...\n");
	while (!(wifi_is_running(STA_WLAN_INDEX) || wifi_is_running(SOFTAP_WLAN_INDEX))) {
		rtos_time_delay_ms(500);
	}
	RTK_LOGI(LOG_TAG, "wifi_task: dispatcher up.\n");

	while (1) {
		dashboard_wifi_msg_t msg;
		if (rtos_queue_receive(g_wifi_msg_queue, &msg, 0xFFFFFFFF) == RTK_SUCCESS) {
			dispatch_msg(&msg);
		}
	}
}
