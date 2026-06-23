/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth SPP (Serial Port Profile) server interface.
 */

#ifndef __BT_CLASSIC_SPP_H__
#define __BT_CLASSIC_SPP_H__

#include <stdint.h>
#include <stdbool.h>
#include "btm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RFCOMM server channel; must match the channel in the SDP record. */
#define BT_CLASSIC_SPP_SERVER_CHANN     0x03

void bt_classic_spp_init(void);

void bt_classic_spp_handle_event(T_BT_EVENT event_type, void *event_buf,
								 uint16_t buf_len);

bool bt_classic_spp_send(uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __BT_CLASSIC_SPP_H__ */
