/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth HID consumer-control (media keys) device.
 *
 * Report format: [report_id=1][1 key-bits byte]
 *   bit0 = Scan Next Track
 *   bit1 = Scan Previous Track
 *   bit2 = Play/Pause
 */

#ifndef __BT_CLASSIC_HID_H__
#define __BT_CLASSIC_HID_H__

#include <stdint.h>
#include "btm.h"             /* T_BT_EVENT */
#include "dashboard_key.h"   /* key_event_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Init HID; call after bt_mgr_init() and before gap_start_bt_stack(). */
void bt_classic_hid_init(void);

/* Dispatch HID events (event id 0x36xx); other events are ignored. */
void bt_classic_hid_handle_event(T_BT_EVENT event_type, void *event_buf,
								 uint16_t buf_len);

/* Convert a key event into a HID report and send it to the connected host. */
void bt_classic_hid_key_handler(key_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* __BT_CLASSIC_HID_H__ */
