/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_LINK_UTIL_H_
#define _APP_LINK_UTIL_H_

#include <stdint.h>
#include <stdbool.h>
#include "btm.h"
#include "os_queue.h"
#include "audio_type.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @defgroup APP_LINK App Link
  * @brief App Link
  * @{
  */

#define MAX_BR_LINK_NUM                 3 /** max BR/EDR link num */
#define MAX_BLE_LINK_NUM                2 /** max BLE link num */

/** bitmask of profiles */
#define A2DP_PROFILE_MASK               0x00000001    /**< A2DP profile bitmask */
#define AVRCP_PROFILE_MASK              0x00000002    /**< AVRCP profile bitmask */
#define HFP_PROFILE_MASK                0x00000004    /**< HFP profile bitmask */
#define RDTP_PROFILE_MASK               0x00000008    /**< Remote Control vendor profile bitmask */
#define SPP_PROFILE_MASK                0x00000010    /**< SPP profile bitmask */
#define IAP_PROFILE_MASK                0x00000020    /**< iAP profile bitmask */
#define PBAP_PROFILE_MASK               0x00000040    /**< PBAP profile bitmask */
#define HSP_PROFILE_MASK                0x00000080    /**< HSP profile bitmask */
#define HID_PROFILE_MASK                0x00000100    /**< HID profile bitmask */
#define MAP_PROFILE_MASK                0x00000200    /**< MAP profile bitmask */
#define GATT_PROFILE_MASK               0x00008000    /**< GATT profile bitmask */
#define GFPS_PROFILE_MASK               0x00010000    /**< GFPS profile bitmask */
#define XIAOAI_PROFILE_MASK             0x00020000    /**< XIAOAI profile bitmask */
#define AMA_PROFILE_MASK                0x00040000    /**< AMA profile bitmask */
#define AVP_PROFILE_MASK                0x00080000    /**< AVP profile bitmask */
#define DID_PROFILE_MASK                0x80000000    /**< DID profile bitmask */
#define ALL_PROFILE_MASK                0xFFFFFFFF


/**  @brief  APP's Bluetooth BR/EDR link connection database */
typedef struct t_app_br_link
{
    uint8_t             bd_addr[6];
    bool                used;
    uint8_t             id;
    uint8_t             *p_embedded_cmd;
    uint16_t            embedded_cmd_len;
    uint16_t            rfc_frame_size;
    uint8_t             rfc_credit;
    uint32_t            connected_profile;

    uint8_t             tx_event_seqn;
    uint8_t             rx_cmd_seqn;

    bool                cmd_set_enable;
    uint8_t             rtk_vendor_spp_active : 1;
} T_APP_BR_LINK;

typedef void(*P_FUN_LE_LINK_DISC_CB)(uint8_t conn_id, uint8_t local_disc_cause,
                                     uint16_t disc_cause);

typedef struct t_le_disc_cb_entry
{
    struct t_le_disc_cb_entry *p_next;
    P_FUN_LE_LINK_DISC_CB disc_callback;
} T_LE_DISC_CB_ENTRY;


/**  @brief  App define le link connection database */
typedef struct t_app_le_link
{
    uint8_t             bd_addr[6];
    bool                used;
    uint8_t             id;
    uint8_t             *p_embedded_cmd;
    uint16_t            embedded_cmd_len;
    uint16_t            mtu_size;
    uint16_t            conn_handle;
    uint8_t             state;
    uint8_t             conn_id;
    uint8_t             rx_cmd_seqn;
    uint8_t             tx_event_seqn;
    uint8_t             transmit_srv_tx_enable_fg;
    T_OS_QUEUE          disc_cb_list;
    uint8_t             local_disc_cause;
    uint8_t             encryption_status;
    bool                cmd_set_enable;
} T_APP_LE_LINK;

/**
    * @brief  get the BR/EDR link by bluetooth address
    * @param  bd_addr bluetooth address
    * @return the BR/EDR link
    */
T_APP_BR_LINK *app_link_find_br_link(uint8_t *bd_addr);

/**
    * @brief  alloc the BR/EDR link by bluetooth address
    * @param  bd_addr bluetooth address
    * @return the BR/EDR link
    */
T_APP_BR_LINK *app_link_alloc_br_link(uint8_t *bd_addr);

/**
    * @brief  free the BR/EDR link
    * @param  p_link the BR/EDR link
    * @return true: success; false: fail
    */
bool app_link_free_br_link(T_APP_BR_LINK *p_link);

/**
    * @brief  find the BLE link by connected id
    * @param  conn_id BLE link id(slot)
    * @return the BLE link
    */
T_APP_LE_LINK *app_link_find_le_link_by_conn_id(uint8_t conn_id);

/**
    * @brief  alloc the BLE link by link id(slot)
    * @param  conn_id BLE link id(slot)
    * @return the BLE link
    */
T_APP_LE_LINK *app_link_alloc_le_link_by_conn_id(uint8_t conn_id);

/**
    * @brief  free the BLE link
    * @param  p_link the BLE link
    * @return true: success; false: fail
    */
bool app_link_free_le_link(T_APP_LE_LINK *p_link);

bool app_link_reg_le_link_disc_cb(uint8_t conn_id, P_FUN_LE_LINK_DISC_CB p_fun_cb);

/** End of APP_LINK
* @}
*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _APP_LINK_UTIL_H_ */
