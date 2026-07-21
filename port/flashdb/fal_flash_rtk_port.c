/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL Flash Port Layer — RTL87x2G eBadge Platform
 *
 * Implements the four FAL operations (init / read / write / erase)
 * using the Realtek FMC API (fmc_api.h).
 *
 * The addr parameter of fmc_flash_nor_xxx is the Flash physical absolute address (starting from 0x70000000).
 * The FAL ops offset parameter is relative to fal_flash_dev.addr,
 * therefore: abs_addr = RTK_FLASH_START_ADDR + offset
 *
 * This file belongs to the application project porting layer; the FlashDB repository itself does not contain this file.
 */

#include <fal_def.h>
#include <fmc_api.h>
#include "fal_cfg.h"

/* ============================================================
 * FAL ops implementation
 * ============================================================ */

static int rtk_flash_init(void)
{
    /* FMC is initialized by the platform startup flow, no additional operation needed here */
    log_i("RTK onchip flash (FMC) ready, base=0x%08lx size=%luMB",
          RTK_FLASH_START_ADDR, RTK_FLASH_SIZE >> 20);
    return 0;
}

/**
 * @param offset  Offset relative to Flash device start address (RTK_FLASH_START_ADDR)
 */
static int rtk_flash_read(long offset, uint8_t *buf, size_t size)
{
    uint32_t abs_addr = RTK_FLASH_START_ADDR + (uint32_t)offset;

    if (!fmc_flash_nor_read(abs_addr, buf, (uint32_t)size))
    {
        log_e("Flash read failed, abs_addr=0x%08x size=%u", abs_addr, (unsigned)size);
        return -1;
    }
    return (int)size;
}

/**
 * @param offset  Offset relative to Flash device start address
 */
static int rtk_flash_write(long offset, const uint8_t *buf, size_t size)
{
    uint32_t abs_addr = RTK_FLASH_START_ADDR + (uint32_t)offset;

    /* fmc_flash_nor_write's data parameter is void*, the const cast is safe */
    if (!fmc_flash_nor_write(abs_addr, (void *)buf, (uint32_t)size))
    {
        log_e("Flash write failed, abs_addr=0x%08x size=%u", abs_addr, (unsigned)size);
        return -1;
    }
    return (int)size;
}

/**
 * @param offset  Offset relative to Flash device start address (must be 4KB aligned)
 * @param size    Must be a multiple of 4KB
 *
 * fmc_flash_nor_erase erases one sector at a time; loop to handle the entire region.
 */
static int rtk_flash_erase(long offset, size_t size)
{
    uint32_t abs_addr = RTK_FLASH_START_ADDR + (uint32_t)offset;
    uint32_t end_addr = abs_addr + (uint32_t)size;

    for (uint32_t addr = abs_addr; addr < end_addr; addr += RTK_FLASH_BLOCK_SIZE)
    {
        if (!fmc_flash_nor_erase(addr, FMC_FLASH_NOR_ERASE_SECTOR))
        {
            log_e("Flash erase failed, abs_addr=0x%08x", addr);
            return -1;
        }
    }
    return (int)size;
}

/* ============================================================
 * FAL Flash device descriptor
 * Referenced by extern in fal_cfg.h, added to FAL_FLASH_DEV_TABLE
 * ============================================================ */
const struct fal_flash_dev rtk_onchip_flash =
{
    .name       = "onchip_flash",
    .addr       = RTK_FLASH_START_ADDR,
    .len        = RTK_FLASH_SIZE,
    .blk_size   = RTK_FLASH_BLOCK_SIZE,
    .ops        = {
        .init   = rtk_flash_init,
        .read   = rtk_flash_read,
        .write  = rtk_flash_write,
        .erase  = rtk_flash_erase,
    },
    .write_gran = RTK_FLASH_WRITE_GRAN,
};
