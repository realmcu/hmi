/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FAL Flash 移植层 —— RTL87x2G eBadge 平台
 *
 * 使用 Realtek FMC API（fmc_api.h）实现 FAL 所需的
 * init / read / write / erase 四个操作。
 *
 * fmc_flash_nor_xxx 的 addr 参数为 Flash 物理绝对地址（0x70000000 起）。
 * FAL ops 的 offset 参数为相对于 fal_flash_dev.addr 的偏移，
 * 因此：abs_addr = RTK_FLASH_START_ADDR + offset
 *
 * 此文件属于应用工程移植层，FlashDB 仓库本身不包含此文件。
 */

#include <fal_def.h>
#include <fmc_api.h>
#include "fal_cfg.h"

/* ============================================================
 * FAL ops 实现
 * ============================================================ */

static int rtk_flash_init(void)
{
    /* FMC 由平台启动流程初始化，此处无需额外操作 */
    log_i("RTK onchip flash (FMC) ready, base=0x%08lx size=%luMB",
          RTK_FLASH_START_ADDR, RTK_FLASH_SIZE >> 20);
    return 0;
}

/**
 * @param offset  相对于 Flash 设备起始地址（RTK_FLASH_START_ADDR）的偏移
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
 * @param offset  相对于 Flash 设备起始地址的偏移
 */
static int rtk_flash_write(long offset, const uint8_t *buf, size_t size)
{
    uint32_t abs_addr = RTK_FLASH_START_ADDR + (uint32_t)offset;

    /* fmc_flash_nor_write 的 data 参数为 void*，const 转换安全 */
    if (!fmc_flash_nor_write(abs_addr, (void *)buf, (uint32_t)size))
    {
        log_e("Flash write failed, abs_addr=0x%08x size=%u", abs_addr, (unsigned)size);
        return -1;
    }
    return (int)size;
}

/**
 * @param offset  相对于 Flash 设备起始地址的偏移（需 4KB 对齐）
 * @param size    需为 4KB 整数倍
 *
 * fmc_flash_nor_erase 每次擦除一个扇区，循环处理整个区域。
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
 * FAL Flash 设备描述符
 * 在 fal_cfg.h 中通过 extern 引用，加入 FAL_FLASH_DEV_TABLE
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
