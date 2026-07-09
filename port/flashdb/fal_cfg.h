/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL configuration file -- eBadge platform
 * Defines Flash device table and partition table; this file is maintained by the application project.
 *
 * Partition plan (base APP_DEFINED_SECTION_ADDR = 0x704C2000, total 60K):
 *   offset 0x0000  size 0x3000 (12K)  kvdb       (env + BF directory)
 *   offset 0x3000  size 0x5000 (20K)  fdb_tsdb1  (log)
 *   offset 0x8000  size 0x7000 (28K)  bf_data    (BF large file data area)
 * All offsets/sizes are 4K aligned (Flash sector boundary).
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include "flash_map.h"

 /* Flash device name, consistent with fal_flash_rtk_port.c */
#define RTK_ONCHIP_FLASH_DEV_NAME  "onchip_flash"

 /* Flash physical parameters, referenced by fal_flash_rtk_port.c to eliminate duplicate definitions */
 #define RTK_FLASH_START_ADDR  0x70000000UL  /* NOR Flash XIP physical base address */
#define RTK_FLASH_SIZE        0x01000000UL  /* 16MB */
 #define RTK_FLASH_BLOCK_SIZE  0x1000        /* 4KB sector */
 #define RTK_FLASH_WRITE_GRAN  1             /* NOR Flash write granularity */

 /* ===================== FAL Flash device table ===================== */
extern const struct fal_flash_dev rtk_onchip_flash;

#define FAL_FLASH_DEV_TABLE  \
    {                            \
        &rtk_onchip_flash,       \
    }

 /* ===================== FAL partition table (static configuration mode) ===================== */
 /* Define this macro to tell fal_partition.c to use the static partition table below, not the dynamic one */
#define FAL_PART_HAS_TABLE_CFG

 /* offset is relative to the Flash device base address (RTK_FLASH_START_ADDR)
  * the three partitions are tightly packed within APP_DEFINED_SECTION, total = 12K+20K+28K = 60K */
#define _APP_SEC_BASE_OFFSET   (APP_DEFINED_SECTION_ADDR - RTK_FLASH_START_ADDR)

#define KVDB_PART_OFFSET       (_APP_SEC_BASE_OFFSET + 0x0000)
#define KVDB_PART_SIZE         0x3000   /* 12K */

#define TSDB_PART_OFFSET       (_APP_SEC_BASE_OFFSET + 0x3000)
#define TSDB_PART_SIZE         0x5000   /* 20K */

#define BF_DATA_PART_OFFSET    (_APP_SEC_BASE_OFFSET + 0x8000)
#define BF_DATA_PART_SIZE      0x7000   /* 28K */

#define FAL_PART_TABLE                                                                                  \
    {                                                                                                   \
        {FAL_PART_MAGIC_WORD, "kvdb",      RTK_ONCHIP_FLASH_DEV_NAME, KVDB_PART_OFFSET,    KVDB_PART_SIZE,    0}, \
        {FAL_PART_MAGIC_WORD, "fdb_tsdb1", RTK_ONCHIP_FLASH_DEV_NAME, TSDB_PART_OFFSET,    TSDB_PART_SIZE,    0}, \
        {FAL_PART_MAGIC_WORD, "bf_data",   RTK_ONCHIP_FLASH_DEV_NAME, BF_DATA_PART_OFFSET, BF_DATA_PART_SIZE, 0}, \
    }

#endif /* _FAL_CFG_H_ */
