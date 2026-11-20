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

/* Send a message to the WiFi task (callable from any context, including ISR) */
bool wifi_task_send_msg(T_WIFI_MSG *p_msg);

/* WiFi module power on/off and RF switch control */
void wifi_enable(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TASK_H_ */
