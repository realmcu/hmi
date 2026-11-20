/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL Flash port layer -- RTL87x2G eBadge platform
 *
 * Uses the Realtek FMC API (fmc_api.h) to implement the FAL-required
 * init / read / write / erase operations.
 *
 * The addr argument of fmc_flash_nor_xxx is a physical Flash address (from 0x70000000).
 * The FAL ops offset argument is relative to fal_flash_dev.addr,
 * hence: abs_addr = RTK_FLASH_START_ADDR + offset
 *
 * This file is part of the application port layer; the FlashDB repo does not contain it.
 */

#include <fal_def.h>
#include <fmc_api.h>

/* Physical Flash parameters (aligned with flash_map.h) */
#define RTK_FLASH_START_ADDR  0x70000000UL  /* Flash physical base address */
#define RTK_FLASH_SIZE        0x01000000UL  /* 16MB */
#define RTK_FLASH_BLOCK_SIZE  0x1000        /* 4KB sector (smallest NOR Flash erase unit) */
#define RTK_FLASH_WRITE_GRAN  1             /* NOR Flash write granularity = 1 bit */

/* ============================================================
 * FAL ops implementation
 * ============================================================ */

static int rtk_flash_init(void)
{
    /* FMC is initialized by the platform boot flow; nothing to do here */
    log_i("RTK onchip flash (FMC) ready, base=0x%08lx size=%luMB",
          RTK_FLASH_START_ADDR, RTK_FLASH_SIZE >> 20);
    return 0;
}

/**
 * @param offset  Offset from the Flash device start address (RTK_FLASH_START_ADDR)
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
 * @param offset  Offset from the Flash device start address
 */
static int rtk_flash_write(long offset, const uint8_t *buf, size_t size)
{
    uint32_t abs_addr = RTK_FLASH_START_ADDR + (uint32_t)offset;

    /* fmc_flash_nor_write takes void *data; casting away const is safe */
    if (!fmc_flash_nor_write(abs_addr, (void *)buf, (uint32_t)size))
    {
        log_e("Flash write failed, abs_addr=0x%08x size=%u", abs_addr, (unsigned)size);
        return -1;
    }
    return (int)size;
}

/**
 * @param offset  Offset from the Flash device start address (4KB aligned)
 * @param size    Must be a multiple of 4KB
 *
 * fmc_flash_nor_erase erases one sector per call, so loop over the whole range.
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
 * Referenced via extern in fal_cfg.h and added to FAL_FLASH_DEV_TABLE
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
