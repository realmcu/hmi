/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <gap.h>
#include <gap_le.h>
#include <gap_bond_le.h>
#include <gap_adv.h>
#include <app_msg.h>
#include "hmi_ble_gap_cb.h"
#include "navi_projection_service.h"
#include "trace.h"
#include "gatt.h"



/** @brief  Default minimum advertising interval when device is discoverable (units of 625us, 160=100ms) */
#define DEFAULT_ADVERTISING_INTERVAL_MIN            320
/** @brief  Default maximum advertising interval */
#define DEFAULT_ADVERTISING_INTERVAL_MAX            320


#define GATT_UUID128_BWPS_ADV   0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14, 0x48, 0x8e, 0x93, 0xD2, 0x17, 0x3C, 0xFF, 0x01, 0x00, 0x00
#define GATT_UUID128_BEACON_SERVICE_ADV           0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14, 0x48, 0x8e, 0x93, 0xD2, 0x17, 0x3C, 0xFF, 0x02, 0x00, 0x00
#define GATT_UUID128_OTA_ADV    0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14, 0x48, 0x8e, 0x93, 0xD2, 0x17, 0x3C, 0xFF, 0xD0, 0x00, 0x00

/* Advertised identity, see PRODUCT spec:
 *   ADV_IND  (31 B, fully used) = flags + 128-bit HMIS UUID + manufacturer data
 *   SCAN_RSP                    = complete local name
 */
#define HMI_ADV_NAME            "HMI-Dashboard"

/* HMIS service 484D4953-0000-1000-8000-00805F9B34FB, little-endian. */
#define HMI_ADV_UUID128_HMIS \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
    0x00, 0x10, 0x00, 0x00, 0x53, 0x49, 0x4D, 0x48

#define HMI_ADV_COMPANY_ID_LO   0x5D
#define HMI_ADV_COMPANY_ID_HI   0x00

/* Manufacturer payload (6 B):
 *   [0] payload version, [1] model (1=RTL8773G 2=RTL8773E 3=RTL8782),
 *   [2] capabilities (bit0 projection, bit1 bonded, bit2 charging),
 *   [3] battery %, [4..5] short device id (BD address low 16 bits, LSB first)
 */
#define HMI_ADV_MODEL_ID        0x02U   /* RTL8773E */
#define HMI_ADV_CAP_PROJECTION  0x01U
#define HMI_ADV_PAYLOAD_IDX     25U     /* 3 (flags) + 18 (uuid) + 4 (mfr hdr) */

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8_t scan_rsp_data[] =
{
    /* Complete local name: 1 (type) + 13 (chars) */
    0x0E,
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'H', 'M', 'I', '-', 'D', 'a', 's', 'h', 'b', 'o', 'a', 'r', 'd',
};

// GAP - Advertisement data (max size = 31 bytes). This array is full, so the
// stack has no room to append the local name; the name lives in scan_rsp_data.
uint8_t adv_data[] =
{
    /* Flags: LE General Discoverable + BR/EDR not supported */
    0x02,
    GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    /* Complete 128-bit UUID list: HMIS service */
    0x11,
    GAP_ADTYPE_128BIT_COMPLETE,
    HMI_ADV_UUID128_HMIS,

    /* Manufacturer specific data: company id + 6-byte payload */
    0x09,
    GAP_ADTYPE_MANUFACTURER_SPECIFIC,
    HMI_ADV_COMPANY_ID_LO, HMI_ADV_COMPANY_ID_HI,
    0x01,                   /* payload version */
    HMI_ADV_MODEL_ID,
    HMI_ADV_CAP_PROJECTION, /* capabilities */
    0x00,                   /* battery %, filled at runtime */
    0x00, 0x00,             /* short device id, filled at runtime */
};



/*============================================================================*
 *                              Functions
 *============================================================================*/
/**
  * @brief  Initialize peripheral and gap bond manager related parameters
  * @return void
  */
void hmi_ble_gap_init(void)
{
    /* [BTM] is the controller-side manager module; its INFO/TRACE lines block on
     * the shared log UART, so keep only errors and warnings. */
    (void)log_module_trace_set(MODULE_BTM, LEVEL_INFO, false);
    (void)log_module_trace_set(MODULE_BTM, LEVEL_TRACE, false);

    /* Device name and device appearance */
    uint8_t  device_name[GAP_DEVICE_NAME_LEN] = HMI_ADV_NAME;
    uint16_t appearance = GAP_GATT_APPEARANCE_WRIST_WORN;
    uint8_t  slave_init_mtu_req = true;



    /* Advertising parameters */
    uint8_t  adv_evt_type = GAP_ADTYPE_ADV_IND;
    uint8_t  adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC;
    uint8_t  adv_direct_addr[GAP_BD_ADDR_LEN] = {0};
    uint8_t  adv_chann_map = GAP_ADVCHAN_ALL;
    uint8_t  adv_filter_policy = GAP_ADV_FILTER_ANY;
    uint16_t adv_int_min = DEFAULT_ADVERTISING_INTERVAL_MIN;
    uint16_t adv_int_max = DEFAULT_ADVERTISING_INTERVAL_MAX;

    /* GAP Bond Manager parameters */
    uint8_t  auth_pair_mode = GAP_PAIRING_MODE_PAIRABLE;
    uint16_t auth_flags = GAP_AUTHEN_BIT_BONDING_FLAG;
    uint8_t  auth_io_cap = GAP_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint8_t  auth_oob = false;
    uint8_t  auth_use_fix_passkey = false;
    uint32_t auth_fix_passkey = 0;

    uint8_t  auth_sec_req_enable = false;
    uint16_t auth_sec_req_flags = GAP_AUTHEN_BIT_BONDING_FLAG;

    /* Set device name and device appearance */
    le_set_gap_param(GAP_PARAM_DEVICE_NAME, GAP_DEVICE_NAME_LEN, device_name);
    le_set_gap_param(GAP_PARAM_APPEARANCE, sizeof(appearance), &appearance);
    le_set_gap_param(GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ, sizeof(slave_init_mtu_req),
                     &slave_init_mtu_req);

    uint8_t phys_prefer = GAP_PHYS_PREFER_ALL;
    uint8_t tx_phys_prefer = GAP_PHYS_PREFER_1M_BIT | GAP_PHYS_PREFER_2M_BIT |
                             GAP_PHYS_PREFER_CODED_BIT;
    uint8_t rx_phys_prefer = GAP_PHYS_PREFER_1M_BIT | GAP_PHYS_PREFER_2M_BIT |
                             GAP_PHYS_PREFER_CODED_BIT;
    //set 2M PHY
    le_set_gap_param(GAP_PARAM_DEFAULT_PHYS_PREFER, sizeof(phys_prefer), &phys_prefer);
    le_set_gap_param(GAP_PARAM_DEFAULT_TX_PHYS_PREFER, sizeof(tx_phys_prefer), &tx_phys_prefer);
    le_set_gap_param(GAP_PARAM_DEFAULT_RX_PHYS_PREFER, sizeof(rx_phys_prefer), &rx_phys_prefer);

    /* Set advertising parameters */
    le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);
    le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR_TYPE, sizeof(adv_direct_type), &adv_direct_type);
    le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR, sizeof(adv_direct_addr), adv_direct_addr);
    le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);
    le_adv_set_param(GAP_PARAM_ADV_FILTER_POLICY, sizeof(adv_filter_policy), &adv_filter_policy);
    le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(adv_int_min), &adv_int_min);
    le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(adv_int_max), &adv_int_max);

    uint8_t bt_bd_addr[6];
    gap_get_param(GAP_PARAM_BD_ADDR, bt_bd_addr);
    /* Short device id: BD address low 16 bits, LSB first. */
    adv_data[HMI_ADV_PAYLOAD_IDX + 4U] = bt_bd_addr[0];
    adv_data[HMI_ADV_PAYLOAD_IDX + 5U] = bt_bd_addr[1];
#ifdef CONFIG_REALTEK_APP_NAVI_PROJECTION
    navi_projection_on_address_ready(bt_bd_addr);
#endif
    le_adv_set_param(GAP_PARAM_ADV_DATA, sizeof(adv_data), adv_data);
    le_adv_set_param(GAP_PARAM_SCAN_RSP_DATA, sizeof(scan_rsp_data), (void *)scan_rsp_data);

    /* Setup the GAP Bond Manager */
    gap_set_param(GAP_PARAM_BOND_PAIRING_MODE, sizeof(auth_pair_mode), &auth_pair_mode);
    gap_set_param(GAP_PARAM_BOND_AUTHEN_REQUIREMENTS_FLAGS, sizeof(auth_flags), &auth_flags);
    gap_set_param(GAP_PARAM_BOND_IO_CAPABILITIES, sizeof(auth_io_cap), &auth_io_cap);
    gap_set_param(GAP_PARAM_BOND_OOB_ENABLED, sizeof(auth_oob), &auth_oob);
    le_bond_set_param(GAP_PARAM_BOND_FIXED_PASSKEY, sizeof(auth_fix_passkey), &auth_fix_passkey);
    le_bond_set_param(GAP_PARAM_BOND_FIXED_PASSKEY_ENABLE, sizeof(auth_use_fix_passkey),
                      &auth_use_fix_passkey);
    le_bond_set_param(GAP_PARAM_BOND_SEC_REQ_ENABLE, sizeof(auth_sec_req_enable), &auth_sec_req_enable);
    le_bond_set_param(GAP_PARAM_BOND_SEC_REQ_REQUIREMENT, sizeof(auth_sec_req_flags),
                      &auth_sec_req_flags);

    /* register gap message callback */
    le_register_app_cb(hmi_ble_gap_callback);

}

/** @} */ /* End of group PERIPH_APP_TASK */


