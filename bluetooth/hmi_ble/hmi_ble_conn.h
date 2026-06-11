/**
*****************************************************************************************
*     Copyright(c) 2017, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file    hmi_ble_conn.h
   * @brief   BLE connection information shared between BLE and protocol layers.
   * @author  howie_wang
   * @date    2026-05-27
   * @version v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2026 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */

#ifndef _HMI_BLE_CONN_H_
#define _HMI_BLE_CONN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** @brief  BLE connection information snapshot. */
typedef struct
{
    uint16_t conn_interval;             /*!< Current connection interval (units: 1.25ms) */
    uint16_t conn_latency;              /*!< Current slave latency */
    uint16_t conn_supervision_timeout;  /*!< Current supervision timeout (units: 10ms) */
    uint16_t conn_mtu_size;             /*!< Current ATT MTU size (bytes) */
} hmi_ble_conn_info_t;

/**
 * @brief  Get a snapshot of the current BLE connection parameters.
 *
 * @param[out] info  Pointer to a hmi_ble_conn_info_t to fill.
 * @return true  if the device is connected and info was populated.
 * @return false if not connected (info is zeroed).
 */
bool hmi_ble_get_conn_info(hmi_ble_conn_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_BLE_CONN_H_ */
