/**
*****************************************************************************************
*     Copyright(c) 2017, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file
   * @brief     This file handles BLE peripheral application routines.
   * @author    jane
   * @date      2017-06-06
   * @version   v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2017 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */

#ifndef _BLE_GAP_INIT_APP__
#define _BLE_GAP_INIT_APP__

#ifdef __cplusplus
extern "C" {
#endif

void hmi_ble_gap_init(void);
void hmi_ble_gap_update_adv_identity(void);
void hmi_ble_gap_start_adv_after_init(void);
void hmi_ble_gap_on_random_addr_result(uint16_t cause);

/** End of PERIPH_APP
* @}
*/


#ifdef __cplusplus
}
#endif

#endif

