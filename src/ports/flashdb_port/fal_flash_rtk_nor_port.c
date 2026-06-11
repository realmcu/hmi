/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL flash device port for Realtek RTK series NOR Flash.
 *
 * The NOR flash is XIP-mapped, but READS MUST go through the FMC controller API
 * (fmc_flash_nor_read), NOT a plain memcpy on the XIP region: the XIP path is
 * cached (CPU D-Cache / flash read-cache), so a memcpy right after a write can
 * return stale data. KVDB/BF read directory entries back immediately after
 * writing them, so a cached read WILL make get_addr fail intermittently with
 * FDB_NOT_FOUND. Writes/erases use the FMC API (fmc_api.h) as usual.
 *
 * Usage:
 *   1. Set RTK_NOR_FLASH_XIP_BASE to the chip's absolute XIP start address
 *      (derive from your linker map or SDK header, e.g. FLASH_BASE_ADDR).
 *   2. Set RTK_NOR_FLASH_TOTAL_SIZE to the total capacity of the flash chip.
 *   3. Declare nor_flash_rtk in fal_cfg.h's FAL_FLASH_DEV_TABLE.
 */

#include <fal.h>
#include "fmc_api.h"
#include <string.h>

/* ──────────────────────────────────────────────────────────────
 * Board-specific constants  ← adjust for your target
 * ────────────────────────────────────────────────────────────── */
#ifndef RTK_NOR_FLASH_XIP_BASE
#error "Define RTK_NOR_FLASH_XIP_BASE in fal_cfg.h (absolute XIP start of the NOR chip)"
#endif

#ifndef RTK_NOR_FLASH_TOTAL_SIZE
#define RTK_NOR_FLASH_TOTAL_SIZE    (8UL * 1024 * 1024)   /* 8 MB — adjust */
#endif

#define RTK_NOR_BLK_SIZE            4096U
#define RTK_NOR_PAGE_SIZE           256U
#define RTK_NOR_WRITE_GRAN          1       /* NOR flash: 1-bit write granularity */

/* ──────────────────────────────────────────────────────────────
 * FAL ops
 *
 * FAL passes 'offset' relative to fal_flash_dev.addr, so
 * abs_addr = RTK_NOR_FLASH_XIP_BASE + offset
 * ────────────────────────────────────────────────────────────── */
static int rtk_nor_init(void)
{
    /* nothing needed: FMC is initialised by the SDK before main() */
    return 0;
}

static int rtk_nor_read(long offset, uint8_t *buf, size_t size)
{
    uint32_t abs_addr = RTK_NOR_FLASH_XIP_BASE + (uint32_t)offset;
    /* no memcpy:XIP go D-Cache/flash read-cache, read after write will get stale value。
    * fmc_flash_nor_read through SPIC directly read flash, ensure data is newest */
    int rc = fmc_flash_nor_read(abs_addr, buf, (uint32_t)size);
    return (rc == 1) ? (int)size : -1;
}

static int rtk_nor_write(long offset, const uint8_t *buf, size_t size)
{
    uint32_t abs_addr = RTK_NOR_FLASH_XIP_BASE + (uint32_t)offset;
    /* fmc_flash_nor_write: returns 1 on success, 0 on failure */
    int rc = fmc_flash_nor_write(abs_addr, (void *)buf, (uint32_t)size);
    return rc == 1 ? (int)size : -1;
}

static int rtk_nor_erase(long offset, size_t size)
{
    uint32_t abs_addr = RTK_NOR_FLASH_XIP_BASE + (uint32_t)offset;
    uint32_t erased = 0;

    while (erased < (uint32_t)size)
    {
        /* fmc_flash_nor_erase: sector-aligned address, returns 1 on success */
        int rc = fmc_flash_nor_erase(abs_addr + erased, FMC_FLASH_NOR_ERASE_SECTOR);
        if (rc != 1)
        {
            return -1;
        }
        erased += RTK_NOR_BLK_SIZE;
    }
    return (int)size;
}

/* ──────────────────────────────────────────────────────────────
 * FAL flash device descriptor
 *
 * Declared extern in fal_cfg.h and referenced in FAL_FLASH_DEV_TABLE.
 * ────────────────────────────────────────────────────────────── */
struct fal_flash_dev nor_flash_rtk =
{
    .name       = "norflash0",
    .addr       = RTK_NOR_FLASH_XIP_BASE,
    .len        = RTK_NOR_FLASH_TOTAL_SIZE,
    .blk_size   = RTK_NOR_BLK_SIZE,
    .ops        = { rtk_nor_init, rtk_nor_read, rtk_nor_write, rtk_nor_erase },
    .write_gran = RTK_NOR_WRITE_GRAN,
};
