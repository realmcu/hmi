/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "ftl.h"
#include "trace.h"
#include "stdint.h"
#include "string.h"
#include "ble_ext_adv.h"
#include "app_ble_adv.h"
#include "simple_ble_service.h"

#define HI_WORD(x)                    ((uint8_t)((x & 0xFF00) >> 8))
#define LO_WORD(x)                    ((uint8_t)(x))

#define APP_STATIC_RANDOM_ADDR_OFFSET 0xC00

static uint8_t adv_handle = 0xFF;
static T_BLE_EXT_ADV_MGR_STATE adv_state = BLE_EXT_ADV_MGR_ADV_DISABLED;

static uint8_t scan_rsp_data[] = {
    0x03,
    GAP_ADTYPE_APPEARANCE,
    LO_WORD(GAP_GATT_APPEARANCE_UNKNOWN),
    HI_WORD(GAP_GATT_APPEARANCE_UNKNOWN),
};

static uint8_t adv_data[] = {
    0x02,
    GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    0x03,
    GAP_ADTYPE_16BIT_COMPLETE,
    LO_WORD(GATT_UUID_SIMPLE_PROFILE),
    HI_WORD(GATT_UUID_SIMPLE_PROFILE),

    0x09,
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'B',
    'L',
    'E',
    '_',
    'N',
    'A',
    'V',
    'I',
};

typedef struct
{
    uint8_t is_exist;
    uint8_t reserved;
    uint8_t bd_addr[GAP_BD_ADDR_LEN];
} T_APP_STATIC_RANDOM_ADDR;

static void app_ble_adv_callback(uint8_t cb_type, void *p_cb_data)
{
    T_BLE_EXT_ADV_CB_DATA cb_data;
    memcpy(&cb_data, p_cb_data, sizeof(T_BLE_EXT_ADV_CB_DATA));

    switch (cb_type)
    {
    case BLE_EXT_ADV_STATE_CHANGE:
    {
        APP_PRINT_TRACE2("app_ble_adv_callback: adv_state %d, adv_handle %d",
                         cb_data.p_ble_state_change->state,
                         cb_data.p_ble_state_change->adv_handle);
        adv_state = cb_data.p_ble_state_change->state;

        if (adv_state == BLE_EXT_ADV_MGR_ADV_ENABLED)
        {
            APP_PRINT_TRACE0("app_ble_adv_callback: BLE_EXT_ADV_MGR_ADV_ENABLED");
        }
        else if (adv_state == BLE_EXT_ADV_MGR_ADV_DISABLED)
        {
            APP_PRINT_TRACE0("app_ble_adv_callback: BLE_EXT_ADV_MGR_ADV_DISABLED");
            APP_PRINT_TRACE2("app_ble_adv_callback: stop cause 0x%x, app cause 0x%02x",
                             cb_data.p_ble_state_change->stop_cause,
                             cb_data.p_ble_state_change->app_cause);
        }
    }
    break;

    case BLE_EXT_ADV_SET_CONN_INFO:
        APP_PRINT_TRACE1("app_ble_adv_callback: BLE_EXT_ADV_SET_CONN_INFO conn_id 0x%x",
                         cb_data.p_ble_conn_info->conn_id);
        break;

    default:
        break;
    }
}

uint32_t app_ble_adv_load_static_random_address(T_APP_STATIC_RANDOM_ADDR *p_addr)
{
    uint32_t result;
    result = ftl_load_from_storage(
        p_addr, APP_STATIC_RANDOM_ADDR_OFFSET, sizeof(T_APP_STATIC_RANDOM_ADDR));
    APP_PRINT_INFO1("app_ble_adv_load_static_random_address: result 0x%x", result);
    if (result)
    {
        memset(p_addr, 0, sizeof(T_APP_STATIC_RANDOM_ADDR));
    }
    return result;
}

uint32_t app_ble_adv_save_static_random_address(T_APP_STATIC_RANDOM_ADDR *p_addr)
{
    APP_PRINT_INFO0("app_ble_adv_save_static_random_address");
    return ftl_save_to_storage(
        p_addr, APP_STATIC_RANDOM_ADDR_OFFSET, sizeof(T_APP_STATIC_RANDOM_ADDR));
}

void app_ble_adv_init_conn_public(void)
{
    T_LE_EXT_ADV_LEGACY_ADV_PROPERTY adv_event_prop = LE_EXT_ADV_LEGACY_ADV_CONN_SCAN_UNDIRECTED;
    uint16_t adv_interval_min = 0xA0;
    uint16_t adv_interval_max = 0xB0;
    T_GAP_LOCAL_ADDR_TYPE own_address_type = GAP_LOCAL_ADDR_LE_PUBLIC;
    T_GAP_REMOTE_ADDR_TYPE peer_address_type = GAP_REMOTE_ADDR_LE_PUBLIC;
    uint8_t peer_address[6] = { 0, 0, 0, 0, 0, 0 };
    T_GAP_ADV_FILTER_POLICY filter_policy = GAP_ADV_FILTER_ANY;

    ble_ext_adv_mgr_init_adv_params(&adv_handle,
                                    adv_event_prop,
                                    adv_interval_min,
                                    adv_interval_max,
                                    own_address_type,
                                    peer_address_type,
                                    peer_address,
                                    filter_policy,
                                    sizeof(adv_data),
                                    adv_data,
                                    sizeof(scan_rsp_data),
                                    scan_rsp_data,
                                    NULL);

    ble_ext_adv_mgr_register_callback(app_ble_adv_callback, adv_handle);
}

void app_ble_adv_init_conn_random(void)
{
    T_LE_EXT_ADV_LEGACY_ADV_PROPERTY adv_event_prop = LE_EXT_ADV_LEGACY_ADV_CONN_SCAN_UNDIRECTED;
    uint16_t adv_interval_min = 0xA0;
    uint16_t adv_interval_max = 0xB0;
    T_GAP_LOCAL_ADDR_TYPE own_address_type = GAP_LOCAL_ADDR_LE_RANDOM;
    T_GAP_REMOTE_ADDR_TYPE peer_address_type = GAP_REMOTE_ADDR_LE_PUBLIC;
    uint8_t peer_address[6] = { 0, 0, 0, 0, 0, 0 };
    T_GAP_ADV_FILTER_POLICY filter_policy = GAP_ADV_FILTER_ANY;

    bool gen_addr = true;
    T_APP_STATIC_RANDOM_ADDR random_addr;

    if (app_ble_adv_load_static_random_address(&random_addr) == 0)
    {
        if ((random_addr.is_exist == true) && ((random_addr.bd_addr[5] & 0xC0) == 0xC0))
        {
            gen_addr = false;
        }
    }
    if (gen_addr)
    {
        if (le_gen_rand_addr(GAP_RAND_ADDR_STATIC, random_addr.bd_addr) == GAP_CAUSE_SUCCESS)
        {
            random_addr.is_exist = true;
            app_ble_adv_save_static_random_address(&random_addr);
        }
    }
    APP_PRINT_INFO1("app_ble_adv_init_conn_random: random address %b",
                    TRACE_BDADDR(random_addr.bd_addr));

    ble_ext_adv_mgr_init_adv_params(&adv_handle,
                                    adv_event_prop,
                                    adv_interval_min,
                                    adv_interval_max,
                                    own_address_type,
                                    peer_address_type,
                                    peer_address,
                                    filter_policy,
                                    sizeof(adv_data),
                                    adv_data,
                                    sizeof(scan_rsp_data),
                                    scan_rsp_data,
                                    random_addr.bd_addr);

    ble_ext_adv_mgr_register_callback(app_ble_adv_callback, adv_handle);
}

bool app_ble_adv_start(uint16_t duration_10ms)
{
    if (adv_state == BLE_EXT_ADV_MGR_ADV_DISABLED)
    {
        APP_PRINT_INFO0("app_ble_adv_start");
        if (ble_ext_adv_mgr_enable(adv_handle, duration_10ms) == GAP_CAUSE_SUCCESS)
        {
            return true;
        }
        return false;
    }

    APP_PRINT_TRACE0("app_ble_adv_start: Already started");
    return true;
}

bool app_ble_adv_stop(int8_t app_cause)
{
    if (adv_state == BLE_EXT_ADV_MGR_ADV_ENABLED)
    {
        if (ble_ext_adv_mgr_disable(adv_handle, app_cause) == GAP_CAUSE_SUCCESS)
        {
            APP_PRINT_INFO0("app_ble_adv_stop");
            return true;
        }
        return false;
    }

    APP_PRINT_TRACE0("app_ble_adv_stop: Already stopped");
    return true;
}

T_BLE_EXT_ADV_MGR_STATE app_ble_adv_get_state(void)
{
    return ble_ext_adv_mgr_get_adv_state(adv_handle);
}

T_GAP_CAUSE app_ble_adv_update_randomaddr(uint8_t *random_address)
{
    return ble_ext_adv_mgr_set_random(adv_handle, random_address);
}

T_GAP_CAUSE app_ble_adv_update_advdata(uint8_t *p_adv_data, uint16_t adv_data_len)
{
    return ble_ext_adv_mgr_set_adv_data(adv_handle, adv_data_len, p_adv_data);
}

T_GAP_CAUSE app_ble_adv_update_scanrspdata(uint8_t *p_scan_data, uint16_t scan_data_len)
{
    return ble_ext_adv_mgr_set_scan_response_data(adv_handle, scan_data_len, p_scan_data);
}

T_GAP_CAUSE app_ble_adv_update_interval(uint16_t adv_interval)
{
    return ble_ext_adv_mgr_change_adv_interval(adv_handle, adv_interval);
}
