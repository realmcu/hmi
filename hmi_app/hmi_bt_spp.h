/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#ifndef _HMI_BT_SPP_H_
#define _HMI_BT_SPP_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void hmi_bt_spp_init(void);
bool hmi_bt_spp_send(uint8_t *bd_addr, uint8_t *data, uint16_t len);
void hmi_bt_spp_set_rx_cb(void (*cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len));

#ifdef __cplusplus
}
#endif

#endif /* _HMI_BT_SPP_H_ */
