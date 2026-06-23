/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth AVRCP profile (bluetooth_ext / external RTL8761B).
 */

#ifndef __BT_CLASSIC_AVRCP_H__
#define __BT_CLASSIC_AVRCP_H__

#include <stdint.h>
#include "btm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Must be called after bt_mgr_init() and before gap_start_bt_stack(). */
void bt_classic_avrcp_init(void);

void bt_classic_avrcp_handle_event(T_BT_EVENT event_type, void *event_buf,
								   uint16_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* __BT_CLASSIC_AVRCP_H__ */
