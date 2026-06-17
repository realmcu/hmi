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
 * Dashboard OTA HTTP 模块对外接口
 *
 * 这是一个 dashboard_wifi 派发器之上的"业务插件"。对外只暴露：
 *   - 几个默认参数宏（DASHBOARD_OTA_HTTP_DEFAULT_*）
 *   - run_ota_http() —— 给派发器调度时调用，**不要**直接从其他地方调
 *
 * Shell 命令 "ota_http" 在 .c 里通过 CMD_TABLE_DATA_SECTION 自注册，链接器
 * 自动收集，使用者不需要从 .h 里看到它。
 *
 * 使用方式：
 *   - 用户在串口输入 "ota_http [host] [port] [resource]"
 *   - cmd handler 调 dashboard_wifi_request_ota_http(...) 把请求塞进
 *     wifi 派发器队列
 *   - 派发器在自己的任务上下文调用 run_ota_http()，业务真正执行
 *
 * 默认参数说明（dashboard_ota_http.c 也用同一份默认值）：
 *   HOST     — HTTP 服务器 IP。预设为 PC 的 WLAN 适配器 IP。
 *   PORT     — Realtek DownloadServer(HTTP) 默认监听 8082。
 *   RESOURCE — 服务器分发的固件名，对应 build 出来的 ota_all.bin。
 *
 * 用户可以在 shell 里全部覆写：
 *   ota_http                              全用默认
 *   ota_http 192.168.1.100                只覆写 host
 *   ota_http 192.168.1.100 8080           覆写 host + port
 *   ota_http 192.168.1.100 8080 a.bin     全部覆写
 *   ota_http ?                            打印 usage
 * ============================================================================ */

#include "basic_types.h"   /* u16 */

#ifdef __cplusplus
extern "C" {
#endif

#define DASHBOARD_OTA_HTTP_DEFAULT_HOST     "192.168.51.162"
#define DASHBOARD_OTA_HTTP_DEFAULT_PORT     8082
#define DASHBOARD_OTA_HTTP_DEFAULT_RESOURCE "ota_all.bin"

/**
 * @brief 真正执行一次 OTA HTTP 升级的阻塞函数。
 *
 * **只应由 dashboard_wifi 派发器调用**。普通调用方请用
 * dashboard_wifi_request_ota_http() 把请求异步入队。直接调这个函数会让
 * 调用者的任务被阻塞几十秒（HTTP 下载 + flash 写入）。
 *
 * 任意参数传 NULL/空串/0，将自动回退到 DASHBOARD_OTA_HTTP_DEFAULT_*。
 *
 * @param host      HTTP server 地址（IP 字符串）
 * @param port      端口
 * @param resource  HTTP 资源路径
 * @retval 0    成功（注意成功后函数通常不会返回，因为内部会 sys_reset）
 * @retval <0   失败（IP 没就绪 / malloc 失败 / ota_start 失败）
 */
int run_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_OTA_HTTP_H */
