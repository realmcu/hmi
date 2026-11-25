/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_MAIN_H_
#define _APP_MAIN_H_

#include <stdint.h>
#include <stdbool.h>

//#include "app_link_util.h"
//#include "app_device.h"
//#include "voice_prompt.h"
//#include "remote.h"
//#include "hub_internal_charger.h"
//#include "app_a2dp.h"
//#include "app_bond.h"
//#include "bt_a2dp.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** @defgroup APP_MAIN App Main
  * @brief Main entry function for audio sample application.
  * @{
  */

/*============================================================================*
 *                              Macros
 *============================================================================*/
/** @defgroup APP_MAIN_Exported_Macros App Main Macros
    * @{
    */
#define UART_BUD_RX     (P3_0)

#define RWS_PRIMARY_VALID_OK             0x01
#define RWS_SECONDARY_VALID_OK           0x02

/** End of APP_MAIN_Exported_Macros
    * @}
    */

/*============================================================================*
 *                              Types
 *============================================================================*/
/** @defgroup APP_MAIN_Exported_Types App Main Types
    * @{
    */

/** @brief Bud location */
typedef enum
{
    BUD_LOC_UNKNOWN     = 0x00,
    BUD_LOC_IN_CASE     = 0x01,
    BUD_LOC_IN_AIR      = 0x02,
    BUD_LOC_IN_EAR      = 0x03,
} T_BUD_LOCATION;

typedef enum
{
    MODE_NONE                = 0x0,
    MODE_APP_PLAYBACK        = 0x11,
    MODE_APP_A2DP_SRC        = 0x22,
    MODE_APP_A2DP_SNK        = 0x33,
} T_APP_AUDIO_MODE;

/**  @brief  App define global app data structure */
typedef struct
{
//    T_APP_BR_LINK               br_link[MAX_BR_LINK_NUM];
//    T_APP_LE_LINK               le_link[MAX_BLE_LINK_NUM];
    uint16_t                    external_mcu_mtu;
    uint8_t                     local_batt_level;           /**< local battery level */
    uint8_t                     remote_batt_level;          /**< remote battery level */
//    T_BATTERY_INFO              batt;
    uint8_t                     factory_addr[6];            /**< local factory address */
    uint8_t                     avrcp_play_status;

    uint8_t                     wait_resume_a2dp_idx;
    uint8_t                     update_active_a2dp_idx;

//    T_APP_DEVICE_STATE          device_state;

    uint8_t                     first_hf_index;
    uint8_t                     last_hf_index;

    bool                        playback_muted;
    bool                        voice_muted;

    T_APP_AUDIO_MODE            audio_play_mode;
//    T_BT_A2DP_ROLE              a2dp_cur_role;

//    T_APP_TONE_VP_STARTED       tone_vp_status;
    uint8_t                     a2dp_sink_addr[6];
//    T_APP_A2DP_SRC_STATE        a2dp_src_state;
    bool                        audio_pipe_create;
    uint8_t                     sco_interrupt_a2dp;
    uint8_t                     usb_status;
//    T_APP_BOND_DEVICE           bond_device[MAX_BOND_INFO_NUM];
    uint8_t                     acl_reconnect_addr[6];
    uint8_t                     transfer_status;
} T_APP_DB;
/** End of APP_MAIN_Exported_Types
    * @}
    */

/*============================================================================*
 *                              Variables
 *============================================================================*/
/** @defgroup APP_MAIN_Exported_Variables App Main Variables
    * @{
    */
extern T_APP_DB app_db;

//extern void *audio_evt_queue_handle;
//extern void *audio_io_queue_handle;
/** End of APP_MAIN_Exported_Variables
    * @}
    */

/** End of APP_MAIN
* @}
*/


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _APP_MAIN_H_ */
