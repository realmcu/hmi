/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_CMD_H_
#define _APP_CMD_H_

#include <stdint.h>
#include <stdbool.h>

#include "app_report.h"
#include "app_link_util.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** @defgroup APP_CMD App Cmd
  * @brief App Cmd
  * @{
  */

/*============================================================================*
 *                              Macros
 *============================================================================*/
/** @defgroup APP_CMD_Exported_Macros App Cmd Macros
   * @{
   */

#define CMD_SYNC_BYTE                       0xAA

/** End of APP_CMD_Exported_Macros
    * @}
    */
/*============================================================================*
 *                              Types
 *============================================================================*/
/** @defgroup APP_CMD_Exported_Types App Cmd Types
  * @{
  */
/**  @brief  embedded uart, spp or le vendor command type.
  *    <b> Only <b> valid when BT SOC connects to external MCU via data uart, spp or le.
  *    refer to SDK audio sample code for definition
  */
typedef enum
{
    CMD_ACK                             = 0x0000,

    CMD_OTA_DEV_INFO                    = 0x0600,
    CMD_OTA_IMG_VER                     = 0x0601,
    CMD_OTA_START                       = 0x0602,
    CMD_OTA_PACKET                      = 0x0603,
    CMD_OTA_VALID                       = 0x0604,
    CMD_OTA_RESET                       = 0x0605,
    CMD_OTA_ACTIVE_RESET                = 0x0606,
    CMD_OTA_BUFFER_CHECK_ENABLE         = 0x0607,
    CMD_OTA_BUFFER_CHECK                = 0x0608,
    CMD_OTA_IMG_INFO                    = 0x0609,
    CMD_OTA_SECTION_SIZE                = 0x060A,
    CMD_OTA_DEV_EXTRA_INFO              = 0x060B,
    CMD_OTA_PROTOCOL_TYPE               = 0x060C,
    CMD_OTA_GET_RELEASE_VER             = 0x060D,
    CMD_OTA_INACTIVE_BANK_VER           = 0x060E,
    CMD_OTA_COPY_IMG                    = 0x060F,
    CMD_OTA_CHECK_SHA256                = 0x0610,
    CMD_OTA_ROLESWAP                    = 0x0611,
    CMD_OTA_TEST_EN                     = 0x0612,
    CMD_OTA_CHECK_KEY                   = 0x0613,

} T_CMD_ID;

/**  @brief  cmd set status to phone
  */
typedef enum
{
    CMD_SET_STATUS_COMPLETE = 0x00,
    CMD_SET_STATUS_DISALLOW = 0x01,
    CMD_SET_STATUS_UNKNOW_CMD = 0x02,
    CMD_SET_STATUS_PARAMETER_ERROR = 0x03,
    CMD_SET_STATUS_BUSY = 0x04,
    CMD_SET_STATUS_PROCESS_FAIL = 0x05,
    CMD_SET_STATUS_ONE_WIRE_EXTEND = 0x06,
    CMD_SET_STATUS_VERSION_INCOMPATIBLE = 0x07,
    CMD_SET_STATUS_SCENARIO_ERROR = 0x0A,
} T_CMD_SET_STATUS;

/** End of APP_CMD_Exported_Types
    * @}
    */
/*============================================================================*
 *                              Functions
 *============================================================================*/
/** @defgroup APP_CMD_Exported_Functions App Cmd Functions
    * @{
    */
/**
    * @brief  App process uart or embedded spp vendor command.
    * @param  cmd_ptr command type
    * @param  cmd_len command length
    * @param  cmd_path command path is used to distinguish from uart, le, spp or iap channel.
    * @param  rx_seqn recieved command sequence
    * @param  app_idx received rx command device index
    * @return void
    */
void app_cmd_handler(uint8_t *cmd_ptr, uint16_t cmd_len, uint8_t cmd_path, uint8_t rx_seqn,
                     uint8_t app_idx);

/* @brief  app cmd init
*
* @param  void
* @return none
*/
void app_cmd_init(void);

/** @} */ /* End of group APP_CMD_Exported_Functions */
/** End of APP_CMD
* @}
*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _APP_CMD_H_ */
