/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_TASK_H_
#define _WIFI_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "wifi_types.h"

/* 向 WiFi 任务发送消息（可从任意上下文调用，包括 ISR） */
bool wifi_task_send_msg(T_WIFI_MSG *p_msg);

/* WiFi 模块上下电及 RF 开关控制 */
void wifi_enable(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TASK_H_ */
