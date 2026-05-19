#ifndef _HMI_BLE_HMI_H_
#define _HMI_BLE_HMI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <app_msg.h>
#include <gap_le.h>
#include <profile_server_ext.h>
#include <profile_client.h>

void hmi_ble_ctrl_init(void);
int  hmi_ble_ctrl_send(const uint8_t *data, uint16_t len);
int  hmi_ble_ctrl_receive(uint8_t *data, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_BLE_HMI_H_ */
