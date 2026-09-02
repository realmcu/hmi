/*
 * Copyright (c) 2018, Realsil Semiconductor Corporation. All rights reserved.
 */

#ifndef _HMI_BR_EDR_GAP_INIT_H_
#define _HMI_BR_EDR_GAP_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @defgroup APP_GAP App Gap
  * @brief App Gap
  * @{
  */

#include "stdbool.h"
#include "stdint.h"


void hmi_br_edr_gap_init(void);

/**
 * @brief Turn the BR/EDR radio on or off.
 *
 * Off means BT_DEVICE_MODE_IDLE: page scan and inquiry scan both disabled.  While
 * the radio is off the gate stays closed, so the ACL-disconnect handler will not
 * quietly put the device back to DISCOVERABLE_CONNECTABLE behind our back.
 *
 * BT-task context only -- from any other task use hmi_br_edr_request_radio_off().
 */
void hmi_br_edr_set_radio_enabled(bool enable);

/**
 * @brief Ask the BT task to turn the BR/EDR radio off.  Returns false if the
 *        message could not be queued.
 *
 * Only posts a message: the caller has to poll hmi_br_edr_radio_is_idle() to know
 * the controller actually stopped scanning.
 */
bool hmi_br_edr_request_radio_off(void);

/** @brief True once BT_EVENT_DEVICE_MODE_RSP confirmed BT_DEVICE_MODE_IDLE. */
bool hmi_br_edr_radio_is_idle(void);

/** @brief True while the radio has been switched off on purpose. */
bool hmi_br_edr_radio_gate_is_closed(void);

/** @brief BT_EVENT_DEVICE_MODE_RSP hook, called from hmi_br_edr_gap_bt_cback(). */
void hmi_br_edr_radio_mode_rsp(uint8_t dev_mode, uint16_t cause);




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _HMI_BR_EDR_GAP_INIT_H_ */
