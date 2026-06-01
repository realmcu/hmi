/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL 配置文件 —— eBadge 平台
 * 定义 Flash 设备表和分区表，此文件由应用工程维护。
 *
 * 分区规划（直接使用 flash_map.h 中的宏）：
 *   APP_DEFINED_SECTION_ADDR = 0x704C2000
 *   APP_DEFINED_SECTION_SIZE = 0x0000F000  (60K)
 *     - kvdb 分区：占用全部 APP_DEFINED_SECTION_SIZE (60K)
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include "flash_map.h"

/* Flash 设备名称，与 fal_flash_rtk_port.c 中保持一致 */
#define RTK_ONCHIP_FLASH_DEV_NAME  "onchip_flash"

/* RTL87x2G Flash 物理基地址 */
#define FLASH_BASE_ADDR            0x70000000UL

/* ===================== FAL Flash 设备表 ===================== */
extern const struct fal_flash_dev rtk_onchip_flash;

#define FAL_FLASH_DEV_TABLE  \
    {                            \
        &rtk_onchip_flash,       \
    }

/* ===================== FAL 分区表（静态配置模式） ===================== */
/* 定义此宏，告知 fal_partition.c 使用下方静态分区表，而非动态分区表 */
#define FAL_PART_HAS_TABLE_CFG

/* offset 为相对 Flash 设备起始地址（FLASH_BASE_ADDR）的偏移
 * 地址和大小均直接引用 flash_map.h 中的宏，不手写数字 */
#define FAL_PART_TABLE                                                              \
    {                                                                                   \
        {FAL_PART_MAGIC_WORD, "kvdb", RTK_ONCHIP_FLASH_DEV_NAME,                       \
            (APP_DEFINED_SECTION_ADDR - FLASH_BASE_ADDR), APP_DEFINED_SECTION_SIZE, 0}, \
    }

#endif /* _FAL_CFG_H_ */
