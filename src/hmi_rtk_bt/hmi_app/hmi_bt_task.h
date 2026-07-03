/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _HMI_BT_TASK_H_
#define _HMI_BT_TASK_H_
#include "app_msg.h"
#include "stdbool.h"

/* BT feature switches. Comment out to disable. */
#define CONFIG_RTK_BT_BREDR             /* BR/EDR + SPP + SPP OTA */

#define CONFIG_RTK_BR_PROFILE_A2DP
#define CONFIG_RTK_BR_PROFILE_AVRCP
#define CONFIG_RTK_BR_PROFILE_HFP
#define CONFIG_RTK_BR_PROFILE_PAN

/**
 * @brief  Create the BT task.
 */
void hmi_bt_task_init(void);

/**
 * @brief  Post an IO message to the BT task queue.
 * @param  p_msg  Pointer to the IO message to enqueue.
 * @return true   Message enqueued successfully.
 * @return false  Failed to enqueue (queue full or send error).
 */
bool hmi_send_msg_to_bt_task(T_IO_MSG *p_msg);

#endif

