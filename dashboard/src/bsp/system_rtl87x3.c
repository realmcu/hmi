/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <locale.h>
#include <stdlib.h>
#include "rtl876x.h"
#include "section.h"
#include "rom_uuid.h"
#include "trace.h"
//#include "core_cmFunc.h"
#include "version.h"
#include "patch_header_check.h"
#include "mem_config.h"
#include "os_mem.h"
#include "platform_utils.h"
#include "crc16btx.h"
#include "platform_ext.h"
#include "system_status_api.h"
#include "upperstack_compile_stamp.h"
#include "storage.h"
#include "flash_map.h"
#include "ftl.h"

#if (SUPPORT_NAND_FLASH == 1)
#include "nftl.h"
#define NFTL_SIZE   (0x50000)
#define FS_SIZE     (0x6000000)
#endif

#if (SUPPORT_ACCESS_SHM == 1)
#include "os_heap.h"
#endif


/** @defgroup  SYSTEM_INIT System Init
    * @brief Start up code for user application.
    * @{
    */
/*============================================================================*
 *                              Macros
 *============================================================================*/
/** @defgroup SYSTEM_INIT_Exported_Macros System Init Exported Macros
    * @brief
    * @{
    */
/**
* @brief: Macro to enable image encryption, refer to encryption_demo project for detail.
*/
#define ENCRYPT_RAM_CODE 0

#define APP_FAKE_PAYLOAD_LEN (0x78000-DEFAULT_HEADER_SIZE)

/** End of SYSTEM_INIT_Exported_Macros
    * @}
    */

/*============================================================================*
 *                              Variables
 *============================================================================*/
/** @defgroup SYSTEM_INIT_Exported_Variables System Init Exported Variables
    * @{
    */

#if (ENCRYPT_RAM_CODE == 1)
extern char Image$$RAM_TEXT$$RO$$Base[];
extern char Load$$RAM_TEXT$$RO$$Base[];
extern char Load$$RAM_TEXT$$RO$$Length[];
#else
extern char Image$$FLASH_TEXT$$RO$$Base[];
extern char Load$$FLASH_TEXT$$RO$$Base[];
extern char Load$$FLASH_TEXT$$RO$$Length[];
#endif
extern char Load$$LR$$LOAD_FLASH$$Base[];

//#pragma push
//#pragma diag_suppress 1296 /* disable warning 1296(extened constant initialiser used)*/
/**
* @brief: application header.
* @note: items in ENCRYPT_RAM_CODE macro is for encrytion solution only
*/
FLASH_HEADER const T_IMG_HEADER_FORMAT img_header =
{
    .auth =
    {
        .image_mac = {[0 ... 15] = 0xFF},
    },
    .ctrl_header =
    {
#if defined CONFIG_SOC_SERIES_RTL8773D
        .ic_type = 0xA,
#elif defined TARGET_RTL8753GFE
        .ic_type = 0xD,
#elif defined TARGET_RTL8763E
        .ic_type = 0xB,
#elif defined TARGET_RTL8773E
        .ic_type = 0x11,
#endif

        .secure_version = 0,
#if (ENCRYPT_RAM_CODE == 1)
        .ctrl_flag.xip = 0,
        .ctrl_flag.enc = 1,
        .ctrl_flag.load_when_boot = 1,
        .ctrl_flag.enc_load = 0,
        .ctrl_flag.enc_key_select = ENC_KEY_OCEK,
#else
        .ctrl_flag.xip = 1,
        .ctrl_flag.enc = 0,
        .ctrl_flag.load_when_boot = 0,
        .ctrl_flag.enc_load = 0,
        .ctrl_flag.enc_key_select = NULL,
#endif
        .ctrl_flag.not_obsolete = 1,
        .image_id = IMG_MCUAPP,
        .payload_len = APP_FAKE_PAYLOAD_LEN,    //Will modify by build tool later
    },
    .uuid = DEFINE_symboltable_uuid,

#if (ENCRYPT_RAM_CODE == 1)
    /* to be modified based on different user scenario */
    .load_src = (uint32_t)Load$$RAM_TEXT$$RO$$Base,
    .load_dst = (uint32_t)Image$$RAM_TEXT$$RO$$Base,
    .load_len = (uint32_t)Load$$RAM_TEXT$$RO$$Length,
    .exe_base = (uint32_t)Image$$RAM_TEXT$$RO$$Base,
#else
    /* XIP test */
    .load_src = (uint32_t)Load$$FLASH_TEXT$$RO$$Base,
    .load_dst = (uint32_t)Image$$FLASH_TEXT$$RO$$Base,
    .load_len = 0,  //0 indicates all XIP
    .exe_base = (uint32_t)Image$$FLASH_TEXT$$RO$$Base,
#endif
    .image_base = (uint32_t) &Load$$LR$$LOAD_FLASH$$Base,
    .git_ver.sub_version =
    {
        ._version_major = VERSION_MAJOR,
        ._version_minor = VERSION_MINOR,
        ._version_revision = VERSION_REVISION,
        ._version_reserve = 0x7ff & VERSION_BUILDNUM
    },
    .git_ver._version_commitid = VERSION_GCID,
#ifdef CONFIG_SOC_SERIES_RTL8773D
    .ex_info.app_ram_info.app_common_on_addr = HEAP_COMMON_ON_ADDR,
    .ex_info.app_ram_info.app_common_on_size = HEAP_COMMON_ON_SIZE,
    .ex_info.app_ram_info.app_common_off_size = HEAP_COMMON_OFF_SIZE,
    .ex_info.app_ram_info.app_common2_on_addr = 0x00500000,
    .ex_info.app_ram_info.app_common2_on_size = 0,
    .ex_info.app_ram_info.app_dsp_share_block = 0,
#else
    .ex_info.app_ram_info =
    {
        .itcm_heap_addr = HEAP_ITCM1_ADDR,
        .app_ram_data_size = APP_GLOBAL_SIZE,
        .app_heap_data_on_size = HEAP_DATA_ON_SIZE,
#ifdef TARGET_RTL8773E
        .app_heap_data_off_size = HEAP_DATA_OFF_SIZE,
#endif
    }
#endif
};

//#pragma pop

/**
 *@brief header extension for each user application
*/
//FLASH_HEADER_EXT const T_IMAGE_HEADER_EXT header_ext_patch = {};

/** End of SYSTEM_INIT_Exported_Variables
    * @}
    */

/*============================================================================*
 *                              Private Functions
 *============================================================================*/
/** @defgroup SYSTEM_INIT_Exported_Functions System Init Exported Functions
    * @{
    */

#if (SUPPORT_DFU_TASK == 1)
void ota_task_init(void)
{
    if (dfu_check_ota_mode_flag())
    {
        dfu_main();
        dfu_set_ota_mode_flag(false);
        vTaskStartScheduler();
    }
}
#endif // (SUPPORT_DFU_TASK == 1)
#if (FTL_POOL_ENABLE == 1)
int32_t ftl_init(const T_STORAGE_PARTITION_INFO *info)
{
    uint32_t ret = ESUCCESS;
#if (SUPPORT_NAND_FLASH == 0)
#ifdef TARGET_RTL8753GFE
    uint16_t rom_ftl_size = 0x1000;
#else
    uint16_t rom_ftl_size = 0x2000;
#endif

    ret = ftl_pool_init(info->address, info->size, rom_ftl_size);
    if (ret == ESUCCESS)
    {
        ftl_cache_init(128);
    }
#else
    ret = ftl_pool_init(0, info->size, 0);
#endif
    return ret;
}
#endif
static const T_STORAGE_PARTITION_INFO default_partitions[] =
{
    {
        .name = FTL_PARTITION_NAME,
#if (SUPPORT_NAND_FLASH == 1)
        .address = 0,
        .size = NFTL_SIZE,
#else
        .address = FTL_ADDR,
        .size = FTL_SIZE,
#endif
        .perm = STORAGE_PERMISSION_READ,
        .media_type = STORAGE_MEDIA_TYPE_NOR,
        .content_type = STORAGE_CONTENT_TYPE_TEXT,
        .init = ftl_init,
        .read = NULL,
        .write = NULL,
        .erase = NULL,
        .async_read = NULL,
        .async_write = NULL,
        .async_erase = NULL,
    },
#ifdef UPPERSTACK_ADDR
    {
        .name = UPPERSTACK_PARTITION_NAME,
#if (SUPPORT_NAND_FLASH == 0)
        .address = UPPERSTACK_ADDR,
        .size = UPPERSTACK_SIZE,
        .perm = STORAGE_PERMISSION_READ,
#else
        .address = PSRAM_UPPERSTACK_ADDR,
        .size = PSRAM_UPPERSTACK_ADDR,
        .perm = PSRAM_UPPERSTACK_SIZE,
#endif //(SUPPORT_NAND_FLASH == 0)
        .media_type = STORAGE_MEDIA_TYPE_NOR,
        .content_type = STORAGE_CONTENT_TYPE_TEXT,
        .init = NULL,
        .read = NULL,
        .write = NULL,
        .erase = NULL,
        .async_read = NULL,
        .async_write = NULL,
        .async_erase = NULL,
    },
#endif
#ifdef VP_DATA_ADDR
    {
        .name = VP_PARTITION_NAME,
#if (SUPPORT_NAND_FLASH == 0)
        .address = VP_DATA_ADDR,
        .size = VP_DATA_SIZE,
#else
        .address = BANK0_EXT_IMG0_ADDR,
        .size = BANK0_EXT_IMG0_SIZE,
#endif
        .perm = STORAGE_PERMISSION_READ,
        .media_type = STORAGE_MEDIA_TYPE_NOR,
        .content_type = STORAGE_CONTENT_TYPE_RO_DATA,
        .init = NULL,
        .read = NULL,
        .write = NULL,
        .erase = NULL,
        .async_read = NULL,
        .async_write = NULL,
        .async_erase = NULL,
    },
#endif
#if (SUPPORT_NAND_FLASH == 1)
    {
        .name = FILESYSTEM_NAME,
        .size = FS_SIZE,
        .perm = STORAGE_PERMISSION_READ,
        .media_type = STORAGE_MEDIA_TYPE_NOR,
        .content_type = STORAGE_CONTENT_TYPE_TEXT,
        .init = NULL,
        .read = NULL,
        .write = NULL,
        .erase = NULL,
        .async_read = NULL,
        .async_write = NULL,
        .async_erase = NULL,
    },
#endif
};

/** @} */ /* End of group SYSTEM_INIT_Exported_Functions */
/** @} */ /* End of group SYSTEM_INIT */
/*============================================================================*
 *                              Public Functions
 *============================================================================*/
/**
 * @brief  Setup the microcontroller system.
 *         Initialize the System.
 * @param  none
 * @return none
 */
void system_init(void)
{
#if (SUPPORT_DFU_TASK == 1)
    ota_task_init();
#endif // (SUPPORT_DFU_TASK == 1)
    __disable_irq();
    setlocale(LC_ALL, "C");

    extern uint32_t Load$$RAM_VECTOR_TABLE$$RO$$Base;
    extern uint32_t Load$$RAM_VECTOR_TABLE$$RO$$Length;
    extern void Default_Handler(void);
    vector_table_update((IRQ_Fun *)((uint32_t)&Load$$RAM_VECTOR_TABLE$$RO$$Base),
                        (uint32_t)&Load$$RAM_VECTOR_TABLE$$RO$$Length, Default_Handler);

#ifdef CONFIG_SOC_SERIES_RTL8773D
    extern void RamLayoutUpdate(uint32_t common0_size, uint32_t common1_size);
    RamLayoutUpdate(COMMON_SRAM0_CFG_SIZE, COMMON_SRAM1_CFG_SIZE);
#endif

    storage_partition_init(default_partitions,
                           sizeof(default_partitions) / sizeof(default_partitions[0]));

#if (SUPPORT_NAND_FLASH == 1)
//#include "flash_nand_adv_driver.h"
//    extern void (*flash_nand_set_bp_lv)(uint32_t idx, uint8_t bp_lv);
//    flash_nand_set_bp_lv(0, 0);
#include "fmc_api_ext.h"
    fmc_flash_nand_set_bp_lv(0x80000000, 0);
    nftl_init(FTL_ADDR, FTL_SIZE);
#endif

//    ftl_init();

#ifndef NOT_SUPPORT_UPPERSTACK
    uint8_t upperstack_compile_stamp[16] = DEFINE_upperstack_compile_stamp;
    sys_hall_upperstack_ini(upperstack_compile_stamp);
#endif
    uint32_t random_seed = platform_random(0xFFFFFFFF);
    srand(random_seed);
    //comment: to enable debug monitor if desired
    //debug_monitor_enable();
    //debug_monitor_point_set(WATCH_POINT_INDEX0, 0x200000, WATCH_POINT_BYTE, WATCH_POINT_FUNCTION_DADDR_W);


    return;
}
/** @} */ /* End of group SYSTEM_INIT_Exported_Functions */
