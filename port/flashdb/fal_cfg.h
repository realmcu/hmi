/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL configuration file -- eBadge platform
 * Defines the Flash device table and partition table; maintained by the application.
 *
 * Partition layout (using the macros from flash_map.h directly):
 *   APP_DEFINED_SECTION_ADDR = 0x704C2000
 *   APP_DEFINED_SECTION_SIZE = 0x0000F000  (60K)
 *     - kvdb partition: occupies the whole APP_DEFINED_SECTION_SIZE (60K)
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include "flash_map.h"

/* Flash device name, kept in sync with fal_flash_rtk_port.c */
#define RTK_ONCHIP_FLASH_DEV_NAME  "onchip_flash"

/* RTL87x2G Flash physical base address */
#define FLASH_BASE_ADDR            0x70000000UL

/* ===================== FAL Flash device table ===================== */
extern const struct fal_flash_dev rtk_onchip_flash;

#define FAL_FLASH_DEV_TABLE  \
    {                            \
        &rtk_onchip_flash,       \
    }

/* ===================== FAL partition table (static config mode) ===================== */
/* Define this macro so fal_partition.c uses the static table below, not a dynamic one */
#define FAL_PART_HAS_TABLE_CFG

/* offset is relative to the Flash device start address (FLASH_BASE_ADDR);
 * address and size both reference flash_map.h macros, no hard-coded numbers */
#define FAL_PART_TABLE                                                              \
    {                                                                                   \
        {FAL_PART_MAGIC_WORD, "kvdb", RTK_ONCHIP_FLASH_DEV_NAME,                       \
            (APP_DEFINED_SECTION_ADDR - FLASH_BASE_ADDR), APP_DEFINED_SECTION_SIZE, 0}, \
    }

#endif /* _FAL_CFG_H_ */
