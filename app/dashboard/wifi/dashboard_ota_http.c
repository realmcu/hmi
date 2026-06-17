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
 * Dashboard OTA HTTP 业务模块（在 dashboard_wifi 派发器之上的"业务插件"）
 *
 * 这个文件**只**做两件事：
 *   1) 提供 run_ota_http() —— 真正阻塞的下载/烧写流程，由 wifi 派发器在
 *      自己的任务上下文里调用。
 *   2) 提供 shell 命令 "ota_http" —— handler 仅把请求封装好交给
 *      dashboard_wifi_request_ota_http()，立刻返回，绝不阻塞 shell。
 *
 * 跟之前版本相比的简化：
 *   - 不再 spawn 自己的 task：业务跑在 dispatcher 任务上，省一份栈。
 *   - 不再用 g_ota_busy 做单实例守卫：派发器队列单消费者天然串行。
 *   - 不再用 ota_http_args_t struct 跨文件传递：用最朴素的 (host, port,
 *     resource) 三个参数，把模块边界缩到最小。
 *
 * 参考：example/ota/ota_http/example_ota_http.c —— 业务流程跟它几乎一样，
 * 区别只是入口（example 是 ota_task，这里是 run_ota_http 同步调用）。
 * ============================================================================ */

#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "section_config.h"

/* 防御性二次确认时用：派发器入队时检查过 online，但任务起来到真正发 socket
 * 之间可能瞬时断开；run_ota_http 进入后再核一次。 */
#include "lwip_netconf.h"

#include "ota_api.h"

#include "dashboard_wifi.h"
#include "dashboard_ota_http.h"

#define LOG_TAG "DASHBOARD-OTA"

extern void sys_reset(void);

/* ============================================================================
 * run_ota_http —— 真正干活的函数（**阻塞**，几十秒不等）
 *
 * 调用约定：在 dashboard_wifi 的派发器任务上下文执行。期间该 task 不会处理
 * queue 里其他消息，所以 OTA 期间发生的 WiFi 事件会排队，等 OTA 结束后才
 * 被消费。这正是我们要的"串行"语义。
 *
 * 流程对照 example_ota_http.c::ota_task()：
 *   1) 二次检查 IP 在线 → 否则 abort
 *   2) (TrustZone 模式才需要的) secure context
 *   3) 申请 ota_context_t、ota_init、ota_start
 *   4) 成功 → sys_reset 重启进新固件
 *   5) 清理 ctx，return
 *
 * @return 0 成功（但通常不会真的 return，因为成功后会 reset），<0 失败
 * ============================================================================ */
int run_ota_http(const char *host, u16 port, const char *resource)
{
	ota_context_t *ctx = NULL;
	int            ret = -1;

	/* 调用方传空串/NULL 都让我们用默认值，简化上层逻辑 */
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

	/* 强转 const → 非 const 是因为底层 SDK 没改 const，但实际不会写。 */
	ret = ota_init(ctx, (char *)host, port, (char *)resource, OTA_HTTP);
	if (ret != OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ota_init failed\n");
		goto cleanup;
	}

	ret = ota_start(ctx);

	if (ret == OTA_OK) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] success, rebooting...\n");
		rtos_time_delay_ms(20);  /* 让 UART 把 log 吐完 */
		sys_reset();
		/* 不会返回 */
	} else {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] ota_start failed: %d\n", ret);
	}

cleanup:
	ota_deinit(ctx);
	rtos_mem_free(ctx);
	return (ret == OTA_OK) ? 0 : -3;
}

/* ============================================================================
 * shell 命令 handler
 *
 * 签名约定：u32 func(u16 argc, u8 *argv[])
 *   argc = 用户输入的参数个数（不含命令名）
 *   argv[0..argc-1] = 各参数字符串
 *
 * 这个版本只解析参数 → 调 dashboard_wifi_request_ota_http() → 立刻返回。
 * 实际下载在派发器 task 上跑，shell 不会被卡住。
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

	/* 提前检查在线（取自 dashboard_wifi 模块的状态）。
	 * 离线的话直接拒绝，省得入队后再 fail，让用户看到的提示更直接。 */
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

	/* 派发器接收消息时会拷贝字符串到队列里，所以这里传栈/argv 指针都安全。 */
	if (dashboard_wifi_request_ota_http(host, port, resource) != 0) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] enqueue request failed\n");
		return FALSE;
	}

	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[OTA] queued\n");
	return TRUE;
}

/* CMD_TABLE_DATA_SECTION：把命令表放进 .cmd.table.data 段，shell 启动时会
 * 扫这个段把所有翻到的命令收齐。每个模块自己注册自己的命令，互不依赖。 */
CMD_TABLE_DATA_SECTION
const COMMAND_TABLE dashboard_ota_http_cmd_table[] = {
	{"ota_http", cmd_dashboard_ota_http},
};
