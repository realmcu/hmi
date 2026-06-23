/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth A2DP relay interface.
 */

#ifndef __BT_CLASSIC_A2DP_H__
#define __BT_CLASSIC_A2DP_H__

#include <stdint.h>
#include <stdbool.h>
#include "btm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Query/get the headphone link (used by AVRCP to relay volume commands). */
bool bt_classic_relay_is_headphone(uint8_t *addr);
bool bt_classic_relay_get_headphone(uint8_t out[6]);

/* Sync headphone source stream to phone play state (true=resume / false=suspend). */
void bt_classic_relay_set_play_state(bool playing);

/* Must be called after bt_mgr_init() and before gap_start_bt_stack(). */
void bt_classic_a2dp_init(void);

void bt_classic_a2dp_handle_event(T_BT_EVENT event_type, void *event_buf,
								   uint16_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* __BT_CLASSIC_A2DP_H__ */
