/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _HMI_BT_HFP_H_
#define _HMI_BT_HFP_H_

#include <stdint.h>
#include <stdbool.h>

void hmi_bt_hfp_init(void);

/* Called when HF-role call status changes (BT_HFP_CALL_IDLE / INCOMING / ACTIVE) */
void hmi_bt_hfp_set_call_status_cb(void (*cb)(uint8_t *bd_addr, uint8_t status));

/* Called when SCO link is established; air_mode: 2=CVSD, 3=mSBC */
void hmi_bt_hfp_set_sco_conn_cb(void (*cb)(uint8_t *bd_addr, uint8_t air_mode));

/* Called with each SCO audio payload received */
void hmi_bt_hfp_set_sco_data_cb(void (*cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len));

/* Called when SCO link is released */
void hmi_bt_hfp_set_sco_disconn_cb(void (*cb)(uint8_t *bd_addr));

bool hmi_bt_hfp_sco_send(uint8_t *bd_addr, uint16_t seq_num, uint8_t *buf, uint16_t len);

#endif /* _HMI_BT_HFP_H_ */
