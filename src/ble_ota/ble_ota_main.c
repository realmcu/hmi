/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include "os_sched.h"
#include "trace.h"
#include "gap.h"
#include "gap_adv.h"
#include "gap_bond_le.h"
#include "profile_server.h"
#include "gap_msg.h"
#include "simple_ble_service.h"
#include "bas.h"
#include "dis.h"
#include "ota_service.h"
#include "dfu_service.h"
#include "app_task.h"
#include "ota_app.h"
#if F_BT_ANCS_CLIENT_SUPPORT
#include <profile_client.h>
#include <ancs.h>
#endif
#include "board.h"
#include "mem_config.h"
#include "patch_header_check.h"
#include "fmc_api.h"
#include "dfu_common.h"
#include "app_section.h"
#include "os_timer.h"
#if (AON_WDG_ENABLE == 1)
#include "rtl_aon_wdt.h"
#endif

#include "app_flags.h"

#if (DLPS_EN == 1)
#include "pm.h"
#include "io_dlps.h"
#include "app_flags.h"

POWER_CheckResult dlps_allow = POWER_CHECK_PASS;

#endif

/** @defgroup  PERIPH_DEMO_MAIN Peripheral Main
    * @brief Main file to initialize hardware and BT stack and start task scheduling
    * @{
    */

/*============================================================================*
 *                              Constants
 *============================================================================*/
/** @brief  Default minimum advertising interval when device is discoverable (units of 625us, 160=100ms) */
#define DEFAULT_ADVERTISING_INTERVAL_MIN            320  /* 200ms */
/** @brief  Default maximum advertising interval */
#define DEFAULT_ADVERTISING_INTERVAL_MAX            320  /* 200ms */

#if (AON_WDG_ENABLE == 1)
#define TIMER_WAKEUP_DLPS_PERIOD              (AON_WDG_TIME_OUT_PERIOD - 1000)
void *xTimerPeriodWakeupDlps;
#endif
/*============================================================================*
 *                              Variables
 *============================================================================*/

/** @brief  GAP - scan response data (max size = 31 bytes) */
static const uint8_t scan_rsp_data[] =
{
    0x03,                             /* length */
    GAP_ADTYPE_APPEARANCE,            /* type="Appearance" */
    LO_WORD(GAP_GATT_APPEARANCE_UNKNOWN),
    HI_WORD(GAP_GATT_APPEARANCE_UNKNOWN),
};

/** @brief  GAP - Advertisement data (max size = 31 bytes, best kept short to conserve power) */
static const uint8_t adv_data[] =
{
    /* Flags */
    0x02,             /* length */
    GAP_ADTYPE_FLAGS, /* type="Flags" */
    GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    /* Service */
    0x03,             /* length */
    GAP_ADTYPE_16BIT_COMPLETE,
    LO_WORD(GATT_UUID_SIMPLE_PROFILE),
    HI_WORD(GATT_UUID_SIMPLE_PROFILE),
    /* Local name */
    0x08,             /* length */
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'B', 'L', 'E', '_', 'O', 'T', 'A',
};

/*============================================================================*
 *                              Functions
 *============================================================================*/
/**
  * @brief  Initialize peripheral and gap bond manager related parameters
  * @return void
  */
void app_le_gap_init(void)
{
    /* Device name and device appearance */
    uint8_t  device_name[GAP_DEVICE_NAME_LEN] = "BLE_OTA";
    uint16_t appearance = GAP_GATT_APPEARANCE_UNKNOWN;
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
#if F_BT_ANCS_CLIENT_SUPPORT
    uint8_t  auth_sec_req_enable = true;
#else
    uint8_t  auth_sec_req_enable = false;
#endif
    uint16_t auth_sec_req_flags = GAP_AUTHEN_BIT_BONDING_FLAG;

    /* Set device name and device appearance */
    le_set_gap_param(GAP_PARAM_DEVICE_NAME, GAP_DEVICE_NAME_LEN, device_name);
    le_set_gap_param(GAP_PARAM_APPEARANCE, sizeof(appearance), &appearance);
    le_set_gap_param(GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ, sizeof(slave_init_mtu_req),
                     &slave_init_mtu_req);

    /* Set advertising parameters */
    le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);
    le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR_TYPE, sizeof(adv_direct_type), &adv_direct_type);
    le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR, sizeof(adv_direct_addr), adv_direct_addr);
    le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);
    le_adv_set_param(GAP_PARAM_ADV_FILTER_POLICY, sizeof(adv_filter_policy), &adv_filter_policy);
    le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(adv_int_min), &adv_int_min);
    le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(adv_int_max), &adv_int_max);
    le_adv_set_param(GAP_PARAM_ADV_DATA, sizeof(adv_data), (void *)adv_data);
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

#if F_BT_LE_5_0_SET_PHY_SUPPORT
    uint8_t phys_prefer = GAP_PHYS_PREFER_ALL;
    uint8_t tx_phys_prefer = GAP_PHYS_PREFER_1M_BIT | GAP_PHYS_PREFER_2M_BIT |
                             GAP_PHYS_PREFER_CODED_BIT;
    uint8_t rx_phys_prefer = GAP_PHYS_PREFER_1M_BIT | GAP_PHYS_PREFER_2M_BIT |
                             GAP_PHYS_PREFER_CODED_BIT;
    //set 2M PHY
    le_set_gap_param(GAP_PARAM_DEFAULT_PHYS_PREFER, sizeof(phys_prefer), &phys_prefer);
    le_set_gap_param(GAP_PARAM_DEFAULT_TX_PHYS_PREFER, sizeof(tx_phys_prefer), &tx_phys_prefer);
    le_set_gap_param(GAP_PARAM_DEFAULT_RX_PHYS_PREFER, sizeof(rx_phys_prefer), &rx_phys_prefer);
#endif
    /* register gap message callback */
    le_register_app_cb(app_gap_callback);
}

/**
 * @brief  Add GATT services and register callbacks
 * @return void
 */
void app_le_profile_init(void)
{
    server_init(5);
    simp_srv_id = simp_ble_service_add_service(app_profile_callback);
    bas_srv_id  = bas_add_service(app_profile_callback);
    dis_srv_id = dis_add_service(app_profile_callback);
    ota_srv_id  = ota_add_service(app_profile_callback);
    dfu_srv_id = dfu_add_service(app_profile_callback);
    server_register_app_cb(app_profile_callback);
#if F_BT_ANCS_CLIENT_SUPPORT
    client_init(1);
    ancs_init(APP_MAX_LINKS);
#endif
}

/**
 * @brief    pinmux configuration
 * @return   void
 */
// void pinmux_configuration(void)
// {
//     Pinmux_Config(KEY, DWGPIO);
//     return;
// }

/**
 * @brief    pad configuration
 * @return   void
 */
// void pad_configuration(void)
//{
//    Pad_Config(KEY, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
//    return;
//}

/**
 * @brief    Contains the initialization of pinmux settings and pad settings
 * @note     All the pinmux settings and pad settings shall be initiated in this function,
 *           but if legacy driver is used, the initialization of pinmux setting and pad setting
 *           should be peformed with the IO initializing.
 * @return   void
 */
void board_init(void)
{
    // pinmux_configuration();
    // pad_configuration();
}

/**
 * @brief    Contains the initialization of peripherals
 * @note     Both new architecture driver and legacy driver initialization method can be used
 * @return   void
 */
void driver_init(void)
{

}

#if (DLPS_EN == 1)
/**
 * @brief    System_Handler
 * @note     system handle to judge which pin is wake source
 * @return   void
 */
//RAM_FUNCTION
void ble_dlps_wakeup_cb(void)
{
//    DBG_DIRECT("System_Handler");

//    if (System_WakeUpInterruptValue(KEY) == SET)
//    {
//        DBG_DIRECT("P2_4 Wake up");
//        Pad_ClearWakeupINTPendingBit(KEY);
//        System_WakeUpPinDisable(KEY);
//    }

//    if (System_DebounceWakeupStatus(KEY) == SET)
//    {
//        DBG_DIRECT("debounce Wake up");
//    }
}

/**
 * @brief this function will be called before enter DLPS
 *
 *  set PAD and wakeup pin config for enterring DLPS
 *
 * @param none
 * @return none
 * @retval void
*/
RAM_FUNCTION
void app_enter_dlps_config(void)
{
//    Pad_Config(KEY, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
//    System_WakeUpDebounceTime(KEY, 0x8);
//    System_WakeUpPinEnable(KEY, PAD_WAKEUP_POL_LOW, PAD_WAKEUP_DEB_DISABLE);

#if (AON_WDG_ENABLE == 1)
    AON_WDT_Kick(AON_WDT);
#endif
}

/**
 * @brief this function will be called after exit DLPS
 *
 *  set PAD and wakeup pin config for enterring DLPS
 *
 * @param none
 * @return none
 * @retval void
*/
RAM_FUNCTION
void app_exit_dlps_config(void)
{
//    Pad_Config(KEY, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);

}

/**
 * @brief DLPS CallBack function
 * @param none
* @return true : allow enter dlps
 * @retval void
*/
RAM_FUNCTION
POWER_CheckResult app_dlps_check_cb(void)
{
    return dlps_allow;
}
#endif

/**
 * @brief    Contains the power mode settings
 * @return   void
 */
void ble_ota_pwr_mgr_init(void)
{
#if (DLPS_EN == 1)
    // power_check_cb_register(app_dlps_check_cb);
    // DLPS_IORegUserDlpsEnterCb(app_enter_dlps_config);
    // DLPS_IORegUserDlpsExitCb(app_exit_dlps_config);
    //    DLPS_IORegister();

    drv_dlps_check_cbacks_register("ota", app_dlps_check_cb);
    drv_dlps_enter_cbacks_register("ota", app_enter_dlps_config);
    drv_dlps_exit_cbacks_register("ota", app_exit_dlps_config);
    drv_dlps_wakeup_cbacks_register("ota", ble_dlps_wakeup_cb);

    bt_power_mode_set(BTPOWER_DEEP_SLEEP);
    power_mode_set(POWER_DLPS_MODE);
#endif
}

#if (AON_WDG_ENABLE == 1)
void vTimerPeriodWakeupDlpsCallback(void *pxTimer)
{
    APP_PRINT_INFO0("TimerPeriodWakeupDlps timeout!");
}
#endif

/**
 * @brief    sw_timer_init
 * @note     create sw timer
 * @return   void
 */
void sw_timer_init(void)
{
#if (AON_WDG_ENABLE == 1)
    bool retval = false;
    retval = os_timer_create(&xTimerPeriodWakeupDlps, "xTimerPeriodWakeupDlps", 1,
                             TIMER_WAKEUP_DLPS_PERIOD, true, vTimerPeriodWakeupDlpsCallback);

    if (!retval)
    {
        APP_PRINT_INFO1("create xTimerPeriodWakeupDlps retval=%d", retval);
    }
    else
    {
        os_timer_start(&xTimerPeriodWakeupDlps);
        APP_PRINT_INFO0("xTimerPeriodWakeupDlps Start!");
    }
#endif
}

/**
 * @brief    Contains the initialization of all tasks
 * @note     There is only one task in BLE Peripheral APP, thus only one APP task is init here
 * @return   void
 */
void ble_ota_task_init(void)
{
    app_task_init();
}

/**
* @brief  print all images version to check ota whether success.
* @return  void
*/
void print_all_images_version(void)
{
    IMG_ID image_id;
    T_IMAGE_VERSION image_version;

    for (image_id = IMG_DFU_FIRST; image_id < IMG_DFU_MAX; image_id++)
    {
        if (image_id == IMG_OTA)
        {
            if (get_ota_bank_image_version(true, image_id, &image_version))
            {
                APP_PRINT_INFO6("image:0x%x,version =0x%x, sub_version:%d.%d.%d.%d", image_id,
                                image_version.ver_info.version,
                                image_version.ver_info.header_sub_version._version_major,
                                image_version.ver_info.header_sub_version._version_minor,
                                image_version.ver_info.header_sub_version._version_revision,
                                image_version.ver_info.header_sub_version._version_reserve);
            }
            else
            {
                APP_PRINT_INFO1("image:0x%x get_active_bank_image_version fail!!!", image_id);
            }
        }
        else
        {
            if (get_ota_bank_image_version(true, image_id, &image_version))
            {
                APP_PRINT_INFO6("image:0x%x,version =0x%x, sub_version:%d.%d.%d.%d", image_id,
                                image_version.ver_info.version,
                                image_version.ver_info.img_sub_version._version_major,
                                image_version.ver_info.img_sub_version._version_minor,
                                image_version.ver_info.img_sub_version._version_revision,
                                image_version.ver_info.img_sub_version._version_reserve);
            }
            else
            {
                APP_PRINT_INFO1("image:0x%x get_active_bank_image_version fail!!!", image_id);
            }
        }
    }

    for (image_id = IMG_USER_DATA_FIRST; image_id < IMG_USER_DATA_MAX; image_id++)
    {
        uint32_t image_addr = 0;
        dfu_get_user_data_info(image_id, &image_addr, true);

        T_IMG_CTRL_HEADER_FORMAT ctrl_header;
        fmc_flash_nor_read(image_addr + offsetof(T_IMG_HEADER_FORMAT, ctrl_header),
                           &ctrl_header, sizeof(ctrl_header));

        fmc_flash_nor_read(image_addr + offsetof(T_IMG_HEADER_FORMAT, git_ver),
                           &image_version, sizeof(T_IMAGE_VERSION));

        if (!ctrl_header.ctrl_flag.not_ready)
        {
            DBG_DIRECT("image:0x%x,version =0x%x, sub_version:%d.%d.%d.%d", image_id,
                       image_version.ver_info.version,
                       image_version.ver_info.img_sub_version._version_major,
                       image_version.ver_info.img_sub_version._version_minor,
                       image_version.ver_info.img_sub_version._version_revision,
                       image_version.ver_info.img_sub_version._version_reserve);
        }
        else
        {
            DBG_DIRECT("image:0x%x user data is not exist!!!", image_id);
        }
    }
}

void print_flash_layout(void)
{
    IMG_ID image_id;
    uint32_t img_addr = 0;
    uint32_t img_size = 0;

    bool is_enable_bank_switch = is_ota_support_bank_switch();
    APP_PRINT_INFO1("Flash Layout bank switch=%d(0: disable)", is_enable_bank_switch);
    APP_PRINT_INFO1("Active OTA Bank num: %d(0: bank0, 1: bank1)", get_active_bank_num());
    APP_PRINT_INFO2("OTA Bank0: Addr=0x%08x, size=0x%08x",
                    flash_partition_addr_get(PARTITION_FLASH_OTA_BANK_0),
                    flash_partition_size_get(PARTITION_FLASH_OTA_BANK_0));
    APP_PRINT_INFO2("OTA Bank1: Addr=0x%08x, size=0x%08x",
                    flash_partition_addr_get(PARTITION_FLASH_OTA_BANK_1),
                    flash_partition_size_get(PARTITION_FLASH_OTA_BANK_1));

    for (image_id = IMG_DFU_FIRST; image_id < IMG_DFU_MAX; image_id++)
    {
        img_addr = get_header_addr_by_img_id(image_id);
        img_size = get_active_bank_image_size_by_img_id(image_id);
        APP_PRINT_INFO3("Active Bank Image id 0x%x: Addr=0x%08x, size=0x%08x", image_id, img_addr,
                        img_size);
    }
    if (is_enable_bank_switch)
    {
        for (image_id = IMG_DFU_FIRST; image_id < IMG_DFU_MAX; image_id++)
        {
            img_addr = get_temp_ota_bank_img_addr_by_img_id(image_id);
            img_size = get_temp_ota_bank_img_size_by_img_id(image_id);
            APP_PRINT_INFO3("Temp Bank Image id 0x%x: Addr=0x%08x, size=0x%08x", image_id, img_addr, img_size);
        }
    }
    APP_PRINT_INFO2("OCCD:       Addr=0x%08x, size=0x%08x",
                    flash_partition_addr_get(PARTITION_FLASH_OCCD),
                    flash_partition_size_get(PARTITION_FLASH_OCCD));
    APP_PRINT_INFO2("Boot Patch0: Addr=0x%08x, size=0x%08x",
                    flash_partition_addr_get(PARTITION_FLASH_BOOT_PATCH0),
                    flash_partition_size_get(PARTITION_FLASH_BOOT_PATCH0));
    APP_PRINT_INFO2("Boot Patch1: Addr=0x%08x, size=0x%08x",
                    flash_partition_addr_get(PARTITION_FLASH_BOOT_PATCH1),
                    flash_partition_size_get(PARTITION_FLASH_BOOT_PATCH1));
    APP_PRINT_INFO2("FTL:        Addr=0x%08x, size=0x%08x",
                    flash_partition_addr_get(PARTITION_FLASH_FTL),
                    flash_partition_size_get(PARTITION_FLASH_FTL));
    APP_PRINT_INFO2("OTA TEMP:   Addr=0x%08x, size=0x%08x",
                    flash_partition_addr_get(PARTITION_FLASH_OTA_TMP),
                    flash_partition_size_get(PARTITION_FLASH_OTA_TMP));

}

/**
 * @brief    Entry of APP code
 * @return   int (To avoid compile warning)
 */
int ble_ota_main(void)
{
    if (FEATURE_TRUSTZONE_ENABLE)
    {
        DBG_DIRECT("Non-Secure World: BLE OTA app main");
    }
    else
    {
        DBG_DIRECT("Secure World: BLE OTA app main");
    }

    extern uint32_t random_seed_value;
    srand(random_seed_value);

    print_flash_layout();
    print_all_images_version();

    // board_init();
    le_gap_init(APP_MAX_LINKS);
    gap_lib_init();
    app_le_gap_init();
    app_le_profile_init();
    ble_ota_pwr_mgr_init();
    sw_timer_init();
    ble_ota_task_init();
    // os_sched_start();

    return 0;
}
/** @} */ /* End of group PERIPH_DEMO_MAIN */


