/*
 * Copyright (c) 2025 Realtek Semiconductor Corporation. All rights reserved.
 *
 * BT EXT HID Consumer Control profile (bluetooth_ext / bee stack).
 *
 * Functional mirror of ble/dashboard_hid_service/dashboard_hids_cc.c, but built
 * on the bluetooth_ext profile-server framework (server_add_service /
 * T_ATTRIB_APPL) instead of the internal rtk_bt_gatts API. Same HID Consumer
 * Control report map, same characteristic layout and (MITM-authenticated)
 * permissions.
 *
 * Media-key reports (next/prev/play-pause/volume) are sent as notifications on
 * the Input Report characteristic via bt_ext_hids_cc_send_key().
 */

#ifndef __BT_EXT_HIDS_CC_H__
#define __BT_EXT_HIDS_CC_H__

#include <stdint.h>
#include <stdbool.h>

#include "profile_server.h"

/* Consumer Control key bit positions inside the 1-byte input report. The order
 * matches the USAGE order in the report map (bit 0 = first usage). */
typedef enum {
	BT_EXT_HID_CC_SCAN_NEXT_TRACK = 0,
	BT_EXT_HID_CC_SCAN_PREV_TRACK = 1,
	BT_EXT_HID_CC_STOP            = 2,
	BT_EXT_HID_CC_PLAY_PAUSE      = 3,
	BT_EXT_HID_CC_MUTE            = 4,
	BT_EXT_HID_CC_VOLUME_UP       = 5,
	BT_EXT_HID_CC_VOLUME_DOWN     = 6,
} T_BT_EXT_HID_CC_CONSUMER_KEY;

/**
 * @brief Add the HID Consumer Control service to the bee-stack GATT database.
 *
 * Must be called after server_init() and before gap_start_bt_stack().
 *
 * @param[in] p_func  General server callback (P_FUN_SERVER_GENERAL_CB), may be NULL.
 * @return Service id assigned by the stack.
 * @retval 0xFF Operation failure.
 */
T_SERVER_ID bt_ext_hids_cc_add_service(void *p_func);

/**
 * @brief Send a Consumer Control input report (notification) to the peer.
 *
 * Drops the report (no notification sent) if the peer has not enabled the Input
 * Report CCCD or if conn_id is out of range.
 *
 * @param[in] conn_id     Connection id of the link to notify.
 * @param[in] key_bitmap  Bitmap of pressed keys (see T_BT_EXT_HID_CC_CONSUMER_KEY);
 *                        0 means "all keys released".
 */
void bt_ext_hids_cc_send_key(uint8_t conn_id, uint16_t key_bitmap);

/** @brief Send Scan Next Track press (true) / release (false). */
void bt_ext_hids_cc_next_track(uint8_t conn_id, bool press);

/** @brief Send Scan Previous Track press (true) / release (false). */
void bt_ext_hids_cc_prev_track(uint8_t conn_id, bool press);

/** @brief Send Play/Pause press (true) / release (false). */
void bt_ext_hids_cc_play_pause(uint8_t conn_id, bool press);

#endif /* __BT_EXT_HIDS_CC_H__ */
