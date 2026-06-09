/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_MAIN_H_
#define _APP_MAIN_H_

#include <stdint.h>
#include <stdlib.h>
#include "app_link_util.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** @defgroup APP_MAIN App Main
  * @brief Main entry function for audio sample application.
  * @{
  */

/*============================================================================*
 *                              Types
 *============================================================================*/
/** @defgroup APP_MAIN_Exported_Types App Main Types
    * @{
    */

/**  @brief  App define global app data structure */

typedef struct
{
    T_APP_BR_LINK               br_link[MAX_BR_LINK_NUM];
    T_APP_LE_LINK               le_link[MAX_BLE_LINK_NUM];

    uint8_t                     local_batt_level;          /**< local battery level */
    uint8_t                     factory_addr[6];
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

extern void *ota_demo_evt_queue_handle;
extern void *ota_demo_io_queue_handle;
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
