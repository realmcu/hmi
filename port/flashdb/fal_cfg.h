/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL 配置文件 —— eBadge 平台
 * 定义 Flash 设备表和分区表，此文件由应用工程维护。
 *
 * 分区规划（基址 APP_DEFINED_SECTION_ADDR = 0x704C2000, 总 60K）:
 *   offset 0x0000  size 0x3000 (12K)  kvdb       (env + BF 目录)
 *   offset 0x3000  size 0x5000 (20K)  fdb_tsdb1  (log)
 *   offset 0x8000  size 0x7000 (28K)  bf_data    (BF 大文件数据区)
 * 所有 offset/size 均 4K 对齐 (Flash sector 边界)。
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include "flash_map.h"

/* Flash 设备名称，与 fal_flash_rtk_port.c 中保持一致 */
#define RTK_ONCHIP_FLASH_DEV_NAME  "onchip_flash"

/* Flash 物理参数，fal_flash_rtk_port.c 从此处引用，消除重复定义 */
#define RTK_FLASH_START_ADDR  0x70000000UL  /* NOR Flash XIP 物理基地址 */
#define RTK_FLASH_SIZE        0x01000000UL  /* 16MB */
#define RTK_FLASH_BLOCK_SIZE  0x1000        /* 4KB 扇区 */
#define RTK_FLASH_WRITE_GRAN  1             /* NOR Flash 位写入粒度 */

/* ===================== FAL Flash 设备表 ===================== */
extern const struct fal_flash_dev rtk_onchip_flash;

#define FAL_FLASH_DEV_TABLE  \
    {                            \
        &rtk_onchip_flash,       \
    }

/* ===================== FAL 分区表（静态配置模式） ===================== */
/* 定义此宏，告知 fal_partition.c 使用下方静态分区表，而非动态分区表 */
#define FAL_PART_HAS_TABLE_CFG

/* offset 为相对 Flash 设备起始地址（RTK_FLASH_START_ADDR）的偏移
 * 三个分区紧密排列在 APP_DEFINED_SECTION 内，总和 = 12K+20K+28K = 60K */
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
