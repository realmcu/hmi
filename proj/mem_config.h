/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef __MEM_CONFIG__
#define __MEM_CONFIG__

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup MEM_CONFIG Memory Configure
  * @brief Memory configuration for user application
  * @{
  */
// Build Bank Configure
#define BUILD_BANK                  0   //Bank0 as default

//Enable RAM Code
#define FEATURE_RAM_CODE            0

/** @brief Flash-Security feature: 1 is enable, 0 is disable */
#define FEATURE_FLASH_SEC           0

//default disable trustzone
#define FEATURE_TRUSTZONE_ENABLE    0

/*============================================================================*
  *                            Memory layout
  *============================================================================*/
#define SPIC0_ADDR            0x04000000
#define SPIC0_SIZE            (64*1024*1024)
#define SPIC1_ADDR            0x08000000
#define SPIC1_SIZE            (64*1024*1024)
#define SPIC2_ADDR            0x10000000
#define SPIC2_SIZE            (256*1024*1024)

#define ITCM1_ADDR            0x00100000
#define ITCM1_SIZE            (192*1024)

#define DTCM0_ADDR            0x00130000
#define DTCM0_SIZE            (64*1024)

#define DTCM1_ADDR            0x00140000
#define DTCM1_SIZE            (64*1024)

#define DATA_SRAM_ADDR        0x00200000
#define DATA_SRAM_SIZE        (16*1024)

#define BUFFER_RAM_ADDR       0x00280000
#define BUFFER_RAM_SIZE       (48*1024)

#define VECTOR_TABLE_ITEMS    (187)

/*============================================================================*
  *                    Non Secure Buffer RAM layout
  *============================================================================*/
#define BUFFER_ON_GLOBAL_SIZE   (1024 + 512)  //must sync with lowerstack

/* --------------------The following macros should not be modified!------------------------- */
#define BUFFER_ON_HEAP_ADDR             (BUFFER_RAM_ADDR + BUFFER_ON_GLOBAL_SIZE)
#define BUFFER_ON_HEAP_SIZE             (BUFFER_RAM_SIZE - BUFFER_ON_GLOBAL_SIZE)

/*============================================================================*
  *                       Non Secure Ext Data SRAM layout
  *============================================================================*/
#define EXT_DATA_SRAM_GLOBAL_SIZE       (0*1024)

/* --------------------The following macros should not be modified!------------------------- */
#define EXT_DATA_SRAM_GLOBAL_ADDR       DATA_SRAM_ADDR
#define EXT_DATA_SRAM_HEAP_ADDR         (EXT_DATA_SRAM_GLOBAL_ADDR + EXT_DATA_SRAM_GLOBAL_SIZE)
#define EXT_DATA_SRAM_HEAP_SIZE         (DATA_SRAM_SIZE - EXT_DATA_SRAM_GLOBAL_SIZE)


/*=======================================================================================*
  *                                 ITCM1 + DTCM0 + DTCM1 layout
  *=====================================================================================*/
/* RAM(ITCM1):          RTL87x2G size: 192K
 * RAM(DTCM0):          RTL87x2G size:  64K
 * RAM(DTCM1):          RTL87x2G size:  64K
 * Total RAM(TCM):      RTL87x2G size: 320K
example:
   a) non-secure total size:                          243K
      1) non-secure upperstack ram         3K
      2) non-secure APP ram                199K
      3) non-secure tcm heap               41K
   b) non-secure system reserved size:                57K
      1) non-secure rom global             7K - 64B
      2) non-secure lowstack rom global    11K + 64B
      3) non-secure stack patch ram        25K
      4) non-secure patch global and code  14K
   c) secure system reserved size:                    20K
      1) secure boot patch ram             12K
      2) secure main stack                 3K
      3) secure rom global                 5K
*/
#define NS_RAM_UPPERSTACK_SIZE          (3*1024)
/* Static RAM needed by this app is ~95K: LVGL's static pool (LV_MEM_SIZE = 64K,
 * see src/lvgl_port/lv_conf.h) plus the two display draw buffers (~22K) and the
 * usual statics. 80K overflows ER_IRAM_NS, so take 128K from the heap - the app
 * barely uses os_mem_alloc because LVGL manages its own pool. */
#define NS_RAM_APP_SIZE                 (176*1024)

/* --------------------The following macros should not be modified!------------------------- */
#define TCM_START_ADDR                  ITCM1_ADDR
#define TCM_TOTAL_SIZE                  (ITCM1_SIZE + DTCM0_SIZE + DTCM1_SIZE)

#define S_RAM_SYSTEM_RESERVED_SIZE      (20*1024)
#define NS_RAM_SYSTEM_RESERVED_SIZE     (57*1024)
#define NS_RAM_APP_RESERVED_SIZE        (TCM_TOTAL_SIZE - S_RAM_SYSTEM_RESERVED_SIZE - NS_RAM_SYSTEM_RESERVED_SIZE)

#if (S_RAM_SYSTEM_RESERVED_SIZE + NS_RAM_SYSTEM_RESERVED_SIZE + NS_RAM_APP_RESERVED_SIZE > TCM_TOTAL_SIZE)
#error "TCM Config size error"
#endif

/* --------------------The following macros should not be modified!------------------------- */
#define NS_RAM_UPPERSTACK_ADDR          (TCM_START_ADDR)
#define NS_RAM_APP_ADDR                 (NS_RAM_UPPERSTACK_ADDR + NS_RAM_UPPERSTACK_SIZE)
#define NS_HEAP_ADDR                    (NS_RAM_APP_ADDR + NS_RAM_APP_SIZE)
#define NS_HEAP_SIZE                    (NS_RAM_APP_RESERVED_SIZE - NS_RAM_APP_SIZE - NS_RAM_UPPERSTACK_SIZE)

#if (NS_RAM_UPPERSTACK_SIZE + NS_RAM_APP_SIZE > NS_RAM_APP_RESERVED_SIZE)
#error "non-secure app ram size config error"
#endif

/* --------------------The following macros should not be modified after rom tapout ------------------------- */
#define S_RAM_ROM_GLOBAL_SIZE           (5*1024)
#define S_RAM_MAIN_STACK_SIZE           (3*1024)
#define S_RAM_BOOT_PATCH_SIZE           (12*1024)

#define NS_RAM_PATCH_SIZE               (14*1024)
#define NS_RAM_STACK_PATCH_SIZE         (25*1024)
#define NS_RAM_LOWSTACK_GLOBAL_SIZE     (11*1024 + 64)
#define NS_RAM_ROM_GLOBAL_SIZE          (7*1024 - 64)

#if (S_RAM_ROM_GLOBAL_SIZE + S_RAM_MAIN_STACK_SIZE + S_RAM_BOOT_PATCH_SIZE != S_RAM_SYSTEM_RESERVED_SIZE)
#error "secure system size error!"
#endif

#if (NS_RAM_PATCH_SIZE + NS_RAM_STACK_PATCH_SIZE + NS_RAM_LOWSTACK_GLOBAL_SIZE + NS_RAM_ROM_GLOBAL_SIZE != NS_RAM_SYSTEM_RESERVED_SIZE)
#error "non-secure system size error!"
#endif

/* -------------------------The following macros should not be modified!------------------------------------ */
#define S_RAM_ROM_GLOBAL_ADDR           (TCM_START_ADDR + TCM_TOTAL_SIZE - S_RAM_ROM_GLOBAL_SIZE)
#define S_RAM_MAIN_STACK_START_ADDR     (S_RAM_ROM_GLOBAL_ADDR - S_RAM_MAIN_STACK_SIZE)
#define S_RAM_BOOT_PATCH_ADDR           (S_RAM_MAIN_STACK_START_ADDR - S_RAM_BOOT_PATCH_SIZE)
#define NS_RAM_PATCH_ADDR               (S_RAM_BOOT_PATCH_ADDR - NS_RAM_PATCH_SIZE)
#define NS_RAM_STACK_PATCH_ADDR         (NS_RAM_PATCH_ADDR - NS_RAM_STACK_PATCH_SIZE)
#define NS_RAM_LOWSTACK_GLOBAL_ADDR     (NS_RAM_STACK_PATCH_ADDR - NS_RAM_LOWSTACK_GLOBAL_SIZE)
#define NS_RAM_ROM_GLOBAL_ADDR          (NS_RAM_LOWSTACK_GLOBAL_ADDR - NS_RAM_ROM_GLOBAL_SIZE)

#define S_RAM_VECTOR_ADDR               (S_RAM_ROM_GLOBAL_ADDR)
#define S_RAM_VECTOR_SIZE               (VECTOR_TABLE_ITEMS*4)

/* ------------------------------------------------------------------------------------------------------- */

/** @} */ /* End of group MEM_CONFIG_Exported_Constents */



#ifdef __cplusplus
}
#endif


/** @} */ /* End of group MEM_CONFIG */

#endif
