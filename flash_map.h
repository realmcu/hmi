/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _FLASH_MAP_H_
#define _FLASH_MAP_H_

#define IMG_HDR_SIZE                    0x00000400  //Changable, default 4K, modify in eFuse if needed

#define CFG_FILE_PAYLOAD_LEN            0x00001000  //Fixed


/* ========== High Level Flash Layout Configuration ========== */
#define EQ_FITTING_ADDR                 0x00000000
#define EQ_FITTING_SIZE                 0x00000000  //0K Bytes
#define RSV_ADDR                        0x70000000
#define RSV_SIZE                        0x00002000  //8K Bytes
#define BOOT_PATCH0_ADDR                0x70004000
#define BOOT_PATCH0_SIZE                0x00003000  //12K Bytes
#define BOOT_PATCH1_ADDR                0x70007000
#define BOOT_PATCH1_SIZE                0x00003000  //12K Bytes
#define UPPERSTACK_ADDR                 0x7000A000
#define UPPERSTACK_SIZE                 0x0003F000  //252K Bytes
#define OEM_CFG_ADDR                    0x70002000
#define OEM_CFG_SIZE                    0x00001400  //5K Bytes
#define OTA_BANK0_ADDR                  0x70049000
#define OTA_BANK0_SIZE                  0x002F1000  //3012K Bytes
#define OTA_BANK1_ADDR                  0x00000000
#define OTA_BANK1_SIZE                  0x00000000  //0K Bytes
#define OTA_TMP_ADDR                    0x70342000
#define OTA_TMP_SIZE                    0x00180000  //1536K Bytes
#define APP_DEFINED_SECTION_ADDR        0x704C2000
#define APP_DEFINED_SECTION_SIZE        0x0000F000  //60K Bytes
#define USER_DATA1_ADDR                 0x704D1000
#define USER_DATA1_SIZE                 0x0032F000  //3260K Bytes
#define USER_DATA1_WITH_HEADER          1
#define USER_DATA2_ADDR                 0x00000000
#define USER_DATA2_SIZE                 0x00000000  //0K Bytes
#define USER_DATA2_WITH_HEADER          0
#define USER_DATA3_ADDR                 0x00000000
#define USER_DATA3_SIZE                 0x00000000  //0K Bytes
#define USER_DATA3_WITH_HEADER          0
#define USER_DATA4_ADDR                 0x00000000
#define USER_DATA4_SIZE                 0x00000000  //0K Bytes
#define USER_DATA4_WITH_HEADER          0
#define USER_DATA5_ADDR                 0x00000000
#define USER_DATA5_SIZE                 0x00000000  //0K Bytes
#define USER_DATA5_WITH_HEADER          0
#define USER_DATA6_ADDR                 0x00000000
#define USER_DATA6_SIZE                 0x00000000  //0K Bytes
#define USER_DATA6_WITH_HEADER          0
#define USER_DATA7_ADDR                 0x00000000
#define USER_DATA7_SIZE                 0x00000000  //0K Bytes
#define USER_DATA7_WITH_HEADER          0
#define USER_DATA8_ADDR                 0x00000000
#define USER_DATA8_SIZE                 0x00000000  //0K Bytes
#define USER_DATA8_WITH_HEADER          0
#define FTL_ADDR                        0x7033A000
#define FTL_SIZE                        0x00008000  //32K Bytes

/* ========== OTA Bank0 Flash Layout Configuration ========== */
#define BANK0_OTA_HDR_ADDR              0x70049000
#define BANK0_OTA_HDR_SIZE              0x00000400  //1K Bytes
#define BANK0_STACK_PATCH_ADDR          0x7004A000
#define BANK0_STACK_PATCH_SIZE          0x00032000  //200K Bytes
#define BANK0_SYS_PATCH_ADDR            0x7007C000
#define BANK0_SYS_PATCH_SIZE            0x00022000  //136K Bytes
#define BANK0_APP_ADDR                  0x7009E000
#define BANK0_APP_SIZE                  0x00180000  //1536K Bytes
#define BANK0_DSP_SYS_ADDR              0x7021E000
#define BANK0_DSP_SYS_SIZE              0x0009E000  //632K Bytes
#define BANK0_DSP_APP_ADDR              0x702BC000
#define BANK0_DSP_APP_SIZE              0x00040000  //256K Bytes
#define BANK0_DSP_CFG_ADDR              0x702FC000
#define BANK0_DSP_CFG_SIZE              0x0000A000  //40K Bytes
#define BANK0_APP_CFG_ADDR              0x70306000
#define BANK0_APP_CFG_SIZE              0x00002000  //8K Bytes
#define BANK0_EXT_IMG0_ADDR             0x70308000
#define BANK0_EXT_IMG0_SIZE             0x00032000  //200K Bytes
#define BANK0_EXT_IMG1_ADDR             0x00000000
#define BANK0_EXT_IMG1_SIZE             0x00000000  //0K Bytes
#define BANK0_EXT_IMG2_ADDR             0x00000000
#define BANK0_EXT_IMG2_SIZE             0x00000000  //0K Bytes
#define BANK0_EXT_IMG3_ADDR             0x00000000
#define BANK0_EXT_IMG3_SIZE             0x00000000  //0K Bytes

/* ========== OTA Bank1 Flash Layout Configuration ========== */
#define BANK1_OTA_HDR_ADDR              0x00000000
#define BANK1_OTA_HDR_SIZE              0x00000000  //0K Bytes
#define BANK1_STACK_PATCH_ADDR          0x00000000
#define BANK1_STACK_PATCH_SIZE          0x00000000  //0K Bytes
#define BANK1_SYS_PATCH_ADDR            0x00000000
#define BANK1_SYS_PATCH_SIZE            0x00000000  //0K Bytes
#define BANK1_APP_ADDR                  0x00000000
#define BANK1_APP_SIZE                  0x00000000  //0K Bytes
#define BANK1_DSP_SYS_ADDR              0x00000000
#define BANK1_DSP_SYS_SIZE              0x00000000  //0K Bytes
#define BANK1_DSP_APP_ADDR              0x00000000
#define BANK1_DSP_APP_SIZE              0x00000000  //0K Bytes
#define BANK1_DSP_CFG_ADDR              0x00000000
#define BANK1_DSP_CFG_SIZE              0x00000000  //0K Bytes
#define BANK1_APP_CFG_ADDR              0x00000000
#define BANK1_APP_CFG_SIZE              0x00000000  //0K Bytes
#define BANK1_EXT_IMG0_ADDR             0x00000000
#define BANK1_EXT_IMG0_SIZE             0x00000000  //0K Bytes
#define BANK1_EXT_IMG1_ADDR             0x00000000
#define BANK1_EXT_IMG1_SIZE             0x00000000  //0K Bytes
#define BANK1_EXT_IMG2_ADDR             0x00000000
#define BANK1_EXT_IMG2_SIZE             0x00000000  //0K Bytes
#define BANK1_EXT_IMG3_ADDR             0x00000000
#define BANK1_EXT_IMG3_SIZE             0x00000000  //0K Bytes

#endif /* _FLASH_MAP_H_ */
