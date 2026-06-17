/*
 * Copyright (c) 2025 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Dashboard BT EXT subsystem entry.
 *
 * Mirrors the wifi/ble dashboard modules: a single long-lived task that brings
 * up the bluetooth_ext (RTL8761B) BLE stack, starts advertising as
 * "AMEBAG2_BT_EX", and then services GAP/IO messages. The task is launched from
 * app_example() in dashboard_main.c, guarded by CONFIG_BT_EXT.
 */

#ifndef __DASHBOARD_BT_EXT_H__
#define __DASHBOARD_BT_EXT_H__

void dash_board_bt_ext_task(void *param);

#endif /* __DASHBOARD_BT_EXT_H__ */
