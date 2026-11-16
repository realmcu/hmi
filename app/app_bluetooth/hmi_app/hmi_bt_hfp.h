/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#ifndef _HMI_BT_HFP_H_
#define _HMI_BT_HFP_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HMI_PHONE_IO_MSG_TYPE 0x7000u

typedef enum
{
    HMI_PHONE_DISCONNECTED = 0,
    HMI_PHONE_IDLE = 1,
    HMI_PHONE_INCOMING = 2,
    HMI_PHONE_DIALING = 3,
    HMI_PHONE_ACTIVE = 4,
} hmi_phone_state_t;

bool hmi_phone_dial(const char *number);
bool hmi_phone_answer(void);
bool hmi_phone_hangup(void);
bool hmi_phone_set_muted(bool muted);
bool hmi_phone_set_watch_audio(bool on_watch);
hmi_phone_state_t hmi_phone_get_state(void);
void hmi_phone_get_number(char *out, size_t size);
bool hmi_phone_get_muted(void);
bool hmi_phone_get_watch_audio(void);
void hmi_phone_handle_command(void *command); /* bt_task only; frees command. */

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
