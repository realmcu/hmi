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

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void hmi_ble_gap_init(void);

/**
 * @brief  Copy this device's advertised local name into @p buf (NUL-terminated).
 *
 * Parses the Local Name AD field out of the advertising data, i.e. it returns
 * exactly what the device broadcasts (GAP_PARAM_DEVICE_NAME itself is write-only
 * and cannot be read back).  The name is truncated to fit @p buf if needed.
 *
 * @param buf      destination buffer.
 * @param buf_len  size of @p buf, including room for the NUL.
 * @return true if a Local Name AD field was found and copied.
 */
bool hmi_ble_gap_get_local_name(char *buf, uint8_t buf_len);

/**
 * @brief  Read this device's public BD address.
 * @param  bd_addr  6-octet out buffer, bd_addr[0]=LSB .. bd_addr[5]=MSB.
 * @return true on success.
 */
bool hmi_ble_gap_get_local_addr(uint8_t bd_addr[6]);

/** End of PERIPH_APP
* @}
*/


#ifdef __cplusplus
}
#endif

#endif

