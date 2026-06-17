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
 * Dashboard WiFi 子系统 —— 统一派发器（dispatcher）模式
 *
 * 角色定位（和 ble/dashboard_ble.h 平行）：
 *   - 一个长驻线程 dash_board_wifi_task 充当"WiFi 工人"。
 *   - 所有进入这个线程的工作都通过一个共享 queue：
 *       * SDK 推上来的 WiFi 事件（join_status / dhcp_status）→ 内部回调 enqueue
 *       * 用户敲的 shell 命令（ota_http / 未来的 stream_img …）→ cmd handler enqueue
 *   - 线程顺序消费 queue，**天然串行**：OTA 跑的时候不会被新命令打断，新命令
 *     就在 queue 里排队等。这避免了多任务并发抢 socket / 抢 flash 的麻烦。
 *
 * 跟 BLE 任务的对照：
 *   dashboard_ble.c 的 task 也是 queue 消费者；它的输入只有按键事件。我们这里
 *   的输入更丰富（事件 + 命令），所以用 type-tagged union（见 .c 文件里的
 *   dashboard_wifi_msg_t）来区分消息种类。
 *
 * 业务模块如何接入：
 *   1) 业务模块（比如 dashboard_ota_http.c）写一个 run_xxx(args) 函数，里面
 *      做真正阻塞的工作，运行在 wifi task 上下文。
 *   2) 业务模块的 shell cmd handler 调用 dashboard_wifi_request_xxx(...)
 *      把请求 enqueue 进 wifi task 的队列。
 *   3) wifi task 内部的派发逻辑根据 msg.type 调用 run_xxx。
 *
 * 这样新增功能只动两个地方：业务文件 + dashboard_wifi.c 里加一个 case 分支。
 * ============================================================================ */

#include <stdbool.h>
#include "basic_types.h"   /* u16 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi 派发器线程入口。
 *
 * 由 app_example() 在启动时通过 rtos_task_create 拉起。任务内部：
 *   1) 创建消息 queue（必须先于事件回调注册）
 *   2) 等到 WiFi 子系统跑起来（wifi_is_running）
 *   3) 进入 while(1)，从 queue 阻塞接收 dashboard_wifi_msg_t，按 type 派发
 *
 * 永不返回。栈建议 4KB（要承载 OTA 这类阻塞业务的需求）。
 *
 * @param param  未使用，传 NULL。
 */
void dash_board_wifi_task(void *param);

/**
 * @brief 查询 WiFi/IP 当前是否可用。
 *
 * 由 wifi task 在收到 DHCP_ADDRESS_ASSIGNED / DISCONNECT 等事件时维护。
 * 写者只有 wifi task 自己（单线程串行写），读者是任意 task（cmd handler 等）。
 * 这是经典的 single-writer / multi-reader 场景，volatile 即可保证可见性。
 *
 * 注意：返回的是**最近一次观察到**的状态，不能立即反映瞬时掉线。关键路径
 * （比如 OTA 真正开 socket 之前）建议再调 LwIP_Check_Connectivity 二次确认。
 *
 * @retval true   STA 已关联 AP 且 LwIP 已拿到 IP
 * @retval false  其他状态
 */
bool dashboard_wifi_is_online(void);

/* ----------------------------------------------------------------------------
 * 业务请求 API：把工作请求塞到 wifi task 的队列。
 *
 * 这些函数立刻返回（不阻塞），实际工作由 wifi task 异步执行。所以它们的
 * 返回值只表示"塞进队列成功了吗"，不代表业务最终结果。业务结果通过 RTK_LOG
 * 反馈到串口。
 *
 * 加新业务的方法：
 *   - 在这里加 dashboard_wifi_request_<feature>() 声明
 *   - 在 dashboard_wifi.c 的派发 switch 里加对应 case
 *   - 在新业务的 .c 里实现 run_<feature>(args) 阻塞函数
 * --------------------------------------------------------------------------- */

/**
 * @brief 请求 wifi task 在它的上下文里跑一次 OTA HTTP 升级。
 *
 * 调用者（一般是 cmd handler）填好服务器地址/端口/资源名，函数内部把这些
 * 拷贝到队列消息里。即便调用者的栈里的字符串失效，队列里的副本仍有效。
 *
 * @param host     HTTP server 主机（IP 字符串），传 NULL 用默认值
 * @param port     端口
 * @param resource 资源路径（文件名），传 NULL 用默认值
 * @retval 0       入队成功（业务后续异步执行）
 * @retval <0      入队失败（队列满 / 队列未初始化）
 */
int dashboard_wifi_request_ota_http(const char *host, u16 port, const char *resource);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_WIFI_H */
