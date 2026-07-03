/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _HMI_BR_EDR_GAP_CB_H_
#define _HMI_BR_EDR_GAP_CB_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include "stdbool.h"
#include "btm.h"


void hmi_br_edr_gap_bt_cback(T_BT_EVENT event_type, void *event_buf, uint16_t buf_len);
void hmi_br_edr_gap_common_cb(uint8_t cb_type, void *p_cb_data);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _HMI_BR_EDR_GAP_CB_H_ */
