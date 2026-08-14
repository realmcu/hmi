/**
*****************************************************************************************
*     Copyright(c) 2017, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file      ble_gap_init.c
   * @brief     Routines to create App task and handle events & messages
   * @author    jane
   * @date      2017-06-02
   * @version   v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2017 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */

/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <stddef.h>   /* NULL */
#include <gap.h>
#include <gap_le.h>
#include <gap_bond_le.h>
#include <gap_adv.h>
#include <app_msg.h>
#include "hmi_ble_gap_cb.h"
#include "trace.h"
#include "gatt.h"



/** @brief  Default minimum advertising interval when device is discoverable (units of 625us, 160=100ms) */
#define DEFAULT_ADVERTISING_INTERVAL_MIN            320
/** @brief  Default maximum advertising interval */
#define DEFAULT_ADVERTISING_INTERVAL_MAX            320


/* Protocol V1.2 vendor Primary Service UUID advertised in scan-response:
 * f48affc0-f69a-11e8-8eb2-f2801f1b9fd1, low-address = last hex byte of the
 * string form (Bluetooth spec LE order -- matches asp_svc.c convention). */
#define GATT_UUID128_HMI_SERVICE_ADV \
    0xD1, 0x9F, 0x1B, 0x1F, 0x80, 0xF2, \
    0xB2, 0x8E, 0xE8, 0x11, 0x9A, 0xF6, \
    0xC0, 0xFF, 0x8A, 0xF4

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8_t scan_rsp_data[] =
{
    /* Service: expose the protocol vendor Primary Service UUID so the peer
     * (App / mini-program) can filter for eBadge devices on discovery. */
    17,             /* length     */
    GAP_ADTYPE_128BIT_COMPLETE,            /* type="Complete 128-bit UUIDs available" */
    GATT_UUID128_HMI_SERVICE_ADV,

    /* place holder for Local Name, filled by BT stack. if not present */
    /* BT stack appends Local Name.                                    */
    0x03,           /* length     */
    GAP_ADTYPE_APPEARANCE,            /* type="Appearance" */
    LO_WORD(GAP_GATT_APPEARANCE_WRIST_WORN),
    HI_WORD(GAP_GATT_APPEARANCE_WRIST_WORN),
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
// **note**: adverdata array will used to store device name which can be re-configured, so except to
// the first 5 Bytes, remaining 26 Byte space will used for device name configure.
uint8_t adv_data[] =
{
    /* Core spec. Vol. 3, Part C, Chapter 18 */
    /* Flags */
    0x02,            /* length     */
    GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    /* Local name */
    0x07,           /* length     */
    GAP_ADTYPE_LOCAL_NAME_COMPLETE, /* type="Complete local name" */
    'e', 'B', 'a', 'd', 'g', 'e', /* eBadge */

    /* Manufacture specified data*/
    0x09,           /* length     */
    GAP_ADTYPE_MANUFACTURER_SPECIFIC,
    0xC5, 0xFE,     /* company id */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* mac address, filled at runtime */
};

/* The MAC occupies the final GAP_BD_ADDR_LEN bytes of adv_data[].  Deriving the
 * offset from sizeof() keeps it correct if the Local Name above is ever
 * re-sized -- hard-coded indices silently ran past the end of the array. */
#define ADV_DATA_MAC_OFFSET     (sizeof(adv_data) - GAP_BD_ADDR_LEN)



/*============================================================================*
 *                              Functions
 *============================================================================*/
/**
  * @brief  Initialize peripheral and gap bond manager related parameters
  * @return void
  */
void hmi_ble_gap_init(void)
{
    /* Device name and device appearance */
    uint8_t  device_name[GAP_DEVICE_NAME_LEN] = "eBadge";
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
    /* fill MAC address into manufacturer specific data at runtime, big-endian */
    uint8_t bt_bd_addr[GAP_BD_ADDR_LEN];
    gap_get_param(GAP_PARAM_BD_ADDR, bt_bd_addr);
    for (uint8_t i = 0; i < GAP_BD_ADDR_LEN; i++)
    {
        adv_data[ADV_DATA_MAC_OFFSET + i] = bt_bd_addr[GAP_BD_ADDR_LEN - 1 - i];
    }

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

bool hmi_ble_gap_get_local_name(char *buf, uint8_t buf_len)
{
    if (buf == NULL || buf_len == 0)
    {
        return false;
    }

    /* Walk the AD structures in adv_data[] and copy out the Local Name field.
     * Each AD structure is [len][type][len-1 data bytes]; len covers the type.
     * GAP_PARAM_DEVICE_NAME is write-only, so the broadcast data is the source. */
    uint16_t i = 0;
    while (i < sizeof(adv_data))
    {
        uint8_t len = adv_data[i];
        if (len == 0 || (uint16_t)(i + 1 + len) > sizeof(adv_data))
        {
            break;
        }
        uint8_t type = adv_data[i + 1];
        if (type == GAP_ADTYPE_LOCAL_NAME_COMPLETE ||
            type == GAP_ADTYPE_LOCAL_NAME_SHORT)
        {
            uint8_t name_len = (uint8_t)(len - 1);            /* strip type byte */
            uint8_t n = (name_len < (uint8_t)(buf_len - 1)) ? name_len
                        : (uint8_t)(buf_len - 1);
            for (uint8_t k = 0; k < n; k++)
            {
                buf[k] = (char)adv_data[i + 2 + k];
            }
            buf[n] = '\0';
            return true;
        }
        i = (uint16_t)(i + 1 + len);
    }
    return false;
}

bool hmi_ble_gap_get_local_addr(uint8_t bd_addr[6])
{
    if (bd_addr == NULL)
    {
        return false;
    }
    return (gap_get_param(GAP_PARAM_BD_ADDR, bd_addr) == GAP_CAUSE_SUCCESS);
}

/** @} */ /* End of group PERIPH_APP_TASK */


