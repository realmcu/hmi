/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_BLE_ADV__
#define _APP_BLE_ADV__

#ifdef __cplusplus
extern "C"
{
#endif

#include "stdbool.h"
#include "stdint.h"
#include "ble_ext_adv.h"

/**
 * @brief Configure connectable, undirected advertising with the public device address.
 */
void app_ble_adv_init_conn_public(void);

/**
 * @brief Configure connectable, undirected advertising with a persistent static random address.
 */
void app_ble_adv_init_conn_random(void);

/**
 * @brief Start the configured advertising set.
 *
 * @param duration_10ms Advertising duration in 10 ms units, or 0 to advertise indefinitely.
 * @return true if advertising is active or was started successfully; otherwise false.
 */
bool app_ble_adv_start(uint16_t duration_10ms);

/**
 * @brief Stop the configured advertising set.
 *
 * @param app_cause Application-defined reason reported to the advertising manager.
 * @return true if advertising is stopped or was stopped successfully; otherwise false.
 */
bool app_ble_adv_stop(int8_t app_cause);

/**
 * @brief Get the current advertising manager state.
 *
 * @return Current advertising state.
 */
T_BLE_EXT_ADV_MGR_STATE app_ble_adv_get_state(void);

/**
 * @brief Replace the random address used by the advertising set.
 *
 * @param random_address Six-byte random device address.
 * @return GAP operation result.
 */
T_GAP_CAUSE app_ble_adv_update_randomaddr(uint8_t *random_address);

/**
 * @brief Replace the advertising payload.
 *
 * @param p_adv_data Advertising payload.
 * @param adv_data_len Payload length in bytes.
 * @return GAP operation result.
 */
T_GAP_CAUSE app_ble_adv_update_advdata(uint8_t *p_adv_data, uint16_t adv_data_len);

/**
 * @brief Replace the scan-response payload.
 *
 * @param p_scan_data Scan-response payload.
 * @param scan_data_len Payload length in bytes.
 * @return GAP operation result.
 */
T_GAP_CAUSE app_ble_adv_update_scanrspdata(uint8_t *p_scan_data, uint16_t scan_data_len);

/**
 * @brief Set the advertising interval.
 *
 * @param adv_interval Advertising interval in 0.625 ms units.
 * @return GAP operation result.
 */
T_GAP_CAUSE app_ble_adv_update_interval(uint16_t adv_interval);

#ifdef __cplusplus
}
#endif
#endif
