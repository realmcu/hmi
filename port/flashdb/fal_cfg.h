/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL configuration for FlashDB + BF extension on Realtek RTK NOR Flash.
 *
 * Memory layout (example, adjust offsets to your linker map):
 *
 *  NOR Flash  (XIP base = RTK_NOR_FLASH_XIP_BASE, total 8 MB)
 *  ┌──────────────────────────────────────────────────────┐
 *  │  ... existing firmware / bootloader / OTA regions ...│
 *  ├──────────────────────────────────────────────────────┤  ← FDB_REGION_OFFSET
 *  │  "fdb_kvdb1"   256 KB  (shared KVDB + BF directory)  │
 *  ├──────────────────────────────────────────────────────┤  ← FDB_REGION_OFFSET + 256 KB
 *  │  "bf_data"    ~2.3 MB  (BF large-file data area)     │
 *  └──────────────────────────────────────────────────────┘
 *
 * Derive FDB_REGION_OFFSET from your SDK's memory map.
 * In the eBadge reference, the equivalent region starts at:
 *   MOUNT_DB = 0x240F400 + 0x700000 = 0x2B0F400
 * so if RTK_NOR_FLASH_XIP_BASE = 0x4000000:
 *   FDB_REGION_OFFSET = 0x2B0F400 - 0x4000000  (negative — wrong base)
 * If the NOR flash is mapped lower, adjust RTK_NOR_FLASH_XIP_BASE accordingly.
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

/* ──────────────────────────────────────────────────────────────
 * 1.  NOR Flash chip parameters  ← adjust for your board
 * ────────────────────────────────────────────────────────────── */

/* Absolute XIP base of the NOR chip: the flash base, not a partition address.
 * FAL's part->offset is relative to this base. */
#define RTK_NOR_FLASH_XIP_BASE      0x70000000UL

#define RTK_NOR_FLASH_TOTAL_SIZE    (32UL * 1024 * 1024)   /* 32 MB total flash */

/* ──────────────────────────────────────────────────────────────
 * 2.  FlashDB region layout (offsets from RTK_NOR_FLASH_XIP_BASE)
 *
 * Layout (32 MB; USER_DATA1 is zero and the rest belongs to FlashDB):
 *   KVDB  0x70936000 .. 0x70976000   (256 KB: KV + BF directory)
 *   BF    0x70976000 .. 0x72000000   (22.5 MB: big-file data)
 *
 * XIP base == flash base, so these offsets are also absolute addresses.
 * ────────────────────────────────────────────────────────────── */
#define FDB_REGION_OFFSET           (0x0936000UL)   /* == 0x70936000 absolute */

#define FDB_KVDB_SIZE               (256UL  * 1024)           /* 256 KB for KV + BF directory */
/* BF takes the rest: 0x72000000 - (0x70936000 + 0x40000) = 0x168A000 */
#define FDB_BF_DATA_SIZE            (0x168A000UL)             /* 22.54 MB for big-file data */

/* Derived: partition offsets from the start of the flash device */
#define FDB_KVDB_PART_OFFSET        (FDB_REGION_OFFSET)
#define FDB_BF_DATA_PART_OFFSET     (FDB_REGION_OFFSET + FDB_KVDB_SIZE)

/* ──────────────────────────────────────────────────────────────
 * 3.  FAL flash device table
 * ────────────────────────────────────────────────────────────── */
extern struct fal_flash_dev nor_flash_rtk;

#define FAL_FLASH_DEV_TABLE \
    {                           \
        &nor_flash_rtk,         \
    }

/* ──────────────────────────────────────────────────────────────
 * 4.  FAL partition table
 *
 * "fdb_kvdb1"  — shared KVDB (stores ordinary KVs and BF directory entries)
 * "bf_data"    — BF large-file data area (managed by the rotating allocator)
 * ────────────────────────────────────────────────────────────── */
#define FAL_PART_HAS_TABLE_CFG

#define FAL_PART_TABLE                                                                           \
    {                                                                                                \
        /* other partitions (bootloader, app, OTA ...) go here */                                   \
        {FAL_PART_MAGIC_WORD, "fdb_kvdb1", "norflash0", FDB_KVDB_PART_OFFSET,    FDB_KVDB_SIZE,    0}, \
        {FAL_PART_MAGIC_WORD, "bf_data",   "norflash0", FDB_BF_DATA_PART_OFFSET, FDB_BF_DATA_SIZE, 0}, \
    }

#endif /* _FAL_CFG_H_ */
