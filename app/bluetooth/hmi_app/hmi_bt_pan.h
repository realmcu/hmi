/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#ifndef _HMI_BT_PAN_H_
#define _HMI_BT_PAN_H_

#include <stdint.h>
#include <stdbool.h>

/* local_bd_addr is the device's own Bluetooth address, required by bt_pan_connect_cfm */
void hmi_bt_pan_init(uint8_t *local_bd_addr);

/* Called when PANU/NAP link is established */
void hmi_bt_pan_set_conn_cb(void (*cb)(uint8_t *bd_addr));

/* Called when PANU/NAP link is released */
void hmi_bt_pan_set_disconn_cb(void (*cb)(uint8_t *bd_addr));

/* Called with each received Ethernet frame */
void hmi_bt_pan_set_rx_cb(void (*cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len));

#endif /* _HMI_BT_PAN_H_ */
