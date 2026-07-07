/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#ifndef _HMI_BT_AVRCP_H_
#define _HMI_BT_AVRCP_H_

#include <stdint.h>
#include <stdbool.h>

void hmi_bt_avrcp_init(void);

/* Update local play status so TG can respond to registration requests correctly */
void hmi_bt_avrcp_play_status_update(uint8_t *bd_addr, bool playing);

/* Update local absolute volume (0-127) so TG can respond to registration requests */
void hmi_bt_avrcp_volume_update(uint8_t *bd_addr, uint8_t vol_0_127);

/* Called when remote TG notifies a play status change */
void hmi_bt_avrcp_set_play_status_cb(void (*cb)(uint8_t *bd_addr, uint8_t play_status));

/* Called when remote TG sets absolute volume (0-127) */
void hmi_bt_avrcp_set_volume_cb(void (*cb)(uint8_t *bd_addr, uint8_t vol_0_127));

#endif /* _HMI_BT_AVRCP_H_ */
