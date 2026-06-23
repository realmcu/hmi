/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth HFP call relay (signaling + SCO voice). Phone is AG (we are HF);
 * headphone is HF (we are AG).
 */

#ifndef __BT_CLASSIC_HFP_H__
#define __BT_CLASSIC_HFP_H__

#include <stdint.h>
#include <stdbool.h>
#include "btm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RFCOMM server channels (must be unique across all services; SPP uses 0x03). */
#define BT_CLASSIC_HFP_HF_CHANN     0x01
#define BT_CLASSIC_HSP_HF_CHANN     0x02
#define BT_CLASSIC_HFP_AG_CHANN     0x17
#define BT_CLASSIC_HSP_AG_CHANN     0x16

void bt_classic_hfp_init(void);

void bt_classic_hfp_handle_event(T_BT_EVENT event_type, void *event_buf,
								 uint16_t buf_len);

void bt_classic_hfp_connect_headphone(uint8_t *addr);

#ifdef __cplusplus
}
#endif

#endif /* __BT_CLASSIC_HFP_H__ */
