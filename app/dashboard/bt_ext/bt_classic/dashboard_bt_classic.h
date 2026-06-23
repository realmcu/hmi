/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Public interface for the BT Classic (BR/EDR) A2DP Sink + AVRCP submodule.
 */

#ifndef __DASHBOARD_BT_CLASSIC_H__
#define __DASHBOARD_BT_CLASSIC_H__

#include <stdint.h>
#include "btm.h"
#include "dashboard_key.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Init submodule; call after sys/remote mgr init, before gap_start_bt_stack(). Returns 0 on success. */
int dashboard_bt_classic_init(void);

/* Single BT Manager event entry; dispatches to GAP/A2DP/AVRCP handlers. */
void dashboard_bt_classic_btmgr_callback(T_BT_EVENT event_type, void *event_buf,
										 uint16_t buf_len);

/* Init profiles + SDP records; call after bt_mgr_cback_register(), before gap_start_bt_stack(). */
int dashboard_bt_classic_profile_init(void);

void dashboard_bt_classic_key_handler(key_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* __DASHBOARD_BT_CLASSIC_H__ */
