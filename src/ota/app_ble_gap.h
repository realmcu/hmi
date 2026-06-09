/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_BLE_GAP_H_
#define _APP_BLE_GAP_H_

#include <stdint.h>
#include <stdbool.h>

#include "app_msg.h"
#include "gap.h"
#include "ble_ext_adv.h"
#include "app_link_util.h"
#include "app_adv_stop_cause.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** @defgroup APP_BLE_GAP App Ble Gap
  * @brief App Ble Gap
  * @{
  */
#define RWS_LE_DEFAULT_MIN_CONN_INTERVAL   (12)//12*1.25 = 15ms
#define RWS_LE_DEFAULT_MAX_CONN_INTERVAL   (24)//24*1.25 = 30ms
#define RWS_LE_DEFAULT_SLAVE_LATENCY       (0)
#define RWS_LE_DEFAULT_SUPERVISION_TIMEOUT (500)//500*10 = 5000ms
/**  @brief  App define le link connection state status */
typedef enum
{
    LE_LINK_STATE_DISCONNECTED,
    LE_LINK_STATE_CONNECTING,
    LE_LINK_STATE_CONNECTED,
    LE_LINK_STATE_DISCONNECTING,
} T_LE_LINK_STATE;

/**  @brief  App define le link encryption state */
typedef enum
{
    LE_LINK_UNENCRYPTIONED = 0,
    LE_LINK_ENCRYPTIONED = 1,
    LE_LINK_ERROR = 2,
} T_LE_LINK_ENCRYPTION_STATE;

typedef enum
{
    LE_LOCAL_DISC_CAUSE_UNKNOWN = 0x00,
    LE_LOCAL_DISC_CAUSE_POWER_OFF = 0x01,
    LE_LOCAL_DISC_CAUSE_SWITCH_TO_OTA = 0x02,
    LE_LOCAL_DISC_CAUSE_AUTHEN_FAILED = 0x03,
    LE_LOCAL_DISC_CAUSE_TTS_DISC = 0x04,
    LE_LOCAL_DISC_CAUSE_OTA_RESET = 0x05,
    LE_LOCAL_DISC_CAUSE_GFPS_STOP = 0x06,
    LE_LOCAL_DISC_CAUSE_GFPS_FAILED = 0x07,
    LE_LOCAL_DISC_CAUSE_ROLESWAP = 0x08,
    LE_LOCAL_DISC_CAUSE_SECONDARY = 0x09,
    LE_LOCAL_DISC_CAUSE_TUYA = 0x0A,
    LE_LOCAL_DISC_CAUSE_ENGAGE_STREAM_PRIOIRTY = 0x0B,
} T_LE_LOCAL_DISC_CAUSE;

extern uint8_t scan_rsp_data_len;
extern uint8_t scan_rsp_data[GAP_MAX_LEGACY_ADV_LEN];

void app_ble_gap_param_init(void);
void app_ble_gap_init(void);
bool app_ble_gap_gen_scan_rsp_data(uint8_t *p_scan_len, uint8_t *p_scan_data);
bool app_ble_gap_disconnect(T_APP_LE_LINK *p_link, T_LE_LOCAL_DISC_CAUSE disc_cause);

/**
    * @brief  All the bt gap msg events are pre-handled in this function.
    *         Then the event handling function shall be called according to the subType of IO_MSG.
    * @param  p_io_msg  pointer to io msg
    * @return void
    */
void app_ble_gap_handle_gap_msg(T_IO_MSG *p_io_msg);

/** End of APP_BLE_GAP
* @}
*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _APP_BLE_GAP_H_ */
