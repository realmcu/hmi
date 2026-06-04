/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * file_db NOR Flash port (reference implementation).
 *
 * Strategy
 * --------
 * NOR Flash has two hard constraints: a program can only flip 1 -> 0, and
 * erase works at sector granularity. file_db rewrites directory entries
 * repeatedly (WRITING -> VALID -> DELETED), and the VALID(0xAA) ->
 * DELETED(0x55) transition needs 0 -> 1 bit flips, which require an erase
 * first. So this port splits the region into two parts:
 *
 *   +-------------------+ 0
 *   | dir sectors       |  N whole sectors, holding Superblock + dir table
 *   +-------------------+ data_offset (sector aligned)
 *   | data sectors      |  file data, appended into erased space
 *   +-------------------+ region_size
 *
 * The directory sectors are loaded as a whole into a RAM cache (dir_cache);
 * every read/write against [0, data_offset) hits the cache. Once the cache
 * is dirtied it is written back at sector granularity: erase the sector,
 * then program the whole sector. Data-region writes go straight to flash -
 * append always targets erased space, so no read-modify-write is needed.
 *
 * Prerequisites
 * -------------
 *  1. Call fdb_port_nor_setup() before fdb_init() to inject the Flash HAL
 *     functions and region info.
 *  2. file_db's compile-time FDB_DATA_ALIGN should be >= sector size (e.g.
 *     4096) so data_offset lands on a sector boundary and directory/data
 *     never share a sector.
 *  3. The Flash HAL must guarantee erase / program complete synchronously
 *     (or block until ready inside the callback).
 *
 * Single-threaded
 * ---------------
 * Like the file_db core, this port does no concurrency protection. In a
 * multitasking scenario the caller must lock around the file_db public API.
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "file_db_port.h"
#include "file_db_config.h"

/*============================================================================*
 *                      Flash HAL hooks (user-implemented)
 *============================================================================*/

/* HAL function pointer types. Return 0 on success, non-zero on failure.
 * abs_addr is the absolute address on the chip (not an offset within the
 * file_db region); this port computes it before passing it in. */
typedef int (*fdb_nor_hal_read_t)(uint32_t abs_addr, void *buf, uint32_t len);
typedef int (*fdb_nor_hal_write_t)(uint32_t abs_addr, const void *buf, uint32_t len);
typedef int (*fdb_nor_hal_erase_t)(uint32_t abs_addr_sector_aligned);

typedef struct
{
    fdb_nor_hal_read_t   read;
    fdb_nor_hal_write_t  program;     /* page-program; len bounded by page_size */
    fdb_nor_hal_erase_t  erase_sector;/* erase one sector; arg must be sector aligned */
    uint32_t             base_addr;   /* start address of the file_db region on chip */
    uint32_t             region_size; /* file_db region size                 */
    uint32_t             sector_size; /* e.g. 4096                           */
    uint32_t             page_size;   /* e.g. 256; < sector_size             */
    uint32_t             dir_bytes;   /* matches file_db's data_offset       */
    uint8_t             *dir_cache;   /* size >= align_up(dir_bytes, sector)  */
    uint32_t             dir_cache_capacity;
} fdb_nor_cfg_t;

/*============================================================================*
 *                              Module state
 *============================================================================*/

static fdb_nor_cfg_t s_cfg;
static uint8_t       s_dirty_sector_bitmap[8];   /* up to 64 directory sectors */
static int           s_initialised;

/*============================================================================*
 *                              Helpers
 *============================================================================*/

static inline uint32_t align_down(uint32_t v, uint32_t a) { return v & ~(a - 1u); }
static inline uint32_t align_up(uint32_t v, uint32_t a) { return (v + (a - 1u)) & ~(a - 1u); }

static inline void dirty_set(uint32_t sector_idx)
{
    s_dirty_sector_bitmap[sector_idx >> 3] |= (uint8_t)(1u << (sector_idx & 7u));
}

static inline int dirty_get(uint32_t sector_idx)
{
    return (s_dirty_sector_bitmap[sector_idx >> 3] >> (sector_idx & 7u)) & 1u;
}

static inline void dirty_clear(uint32_t sector_idx)
{
    s_dirty_sector_bitmap[sector_idx >> 3] &= (uint8_t)~(1u << (sector_idx & 7u));
}

static int dir_sector_count(void)
{
    return (int)(align_up(s_cfg.dir_bytes, s_cfg.sector_size) / s_cfg.sector_size);
}

/* Write one directory sector from the RAM cache back to Flash: erase, then
 * program page by page. */
static int flush_dir_sector(uint32_t sector_idx)
{
    uint32_t off    = sector_idx * s_cfg.sector_size;
    uint32_t addr   = s_cfg.base_addr + off;
    uint8_t *src    = s_cfg.dir_cache + off;

    if (s_cfg.erase_sector(addr) != 0) { return -1; }

    uint32_t left = s_cfg.sector_size;
    while (left > 0)
    {
        uint32_t n = (left > s_cfg.page_size) ? s_cfg.page_size : left;
        if (s_cfg.program(addr, src, n) != 0) {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
        addr += n;
        src  += n;
        left -= n;
    }
    dirty_clear(sector_idx);
    return 0;
}

static int flush_all_dirty_sectors(void)
{
    int n = dir_sector_count();
    for (int i = 0; i < n; ++i)
    {
        if (dirty_get((uint32_t)i))
        {
            if (flush_dir_sector((uint32_t)i) != 0) { return -1; }
        }
    }
    return 0;
}

/*============================================================================*
 *                              Port ops
 *============================================================================*/

static int nor_init(void)
{
    if (!s_initialised)                                                  {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
    if (!s_cfg.read || !s_cfg.program || !s_cfg.erase_sector)            {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
    if (s_cfg.region_size == 0 || s_cfg.sector_size == 0)                {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
    if ((s_cfg.sector_size & (s_cfg.sector_size - 1u)) != 0)             {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
    if (s_cfg.page_size == 0 || s_cfg.page_size > s_cfg.sector_size)     {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
    if (s_cfg.dir_bytes == 0 || s_cfg.dir_bytes >= s_cfg.region_size)    {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
    if (!s_cfg.dir_cache)                                                {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }
    if (s_cfg.dir_cache_capacity <
        align_up(s_cfg.dir_bytes, s_cfg.sector_size))                    {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return -1; }

    /* Load the whole directory region into RAM once. All later directory
     * reads hit this cache. */
    FDB_LOG("%s %d", __FUNCTION__, __LINE__);
    uint32_t cached = align_up(s_cfg.dir_bytes, s_cfg.sector_size);
    int rc = s_cfg.read(s_cfg.base_addr, s_cfg.dir_cache, cached);
    if (rc != 0)
    {
        FDB_LOG("%s %d %d", __FUNCTION__, __LINE__, rc);
        return -1;
    }
    memset(s_dirty_sector_bitmap, 0, sizeof(s_dirty_sector_bitmap));
    return 0;
}

static int nor_deinit(void)
{
    return flush_all_dirty_sectors();
}

static int nor_read(uint32_t off, void *buf, uint32_t len)
{
    if (off + len < off || off + len > s_cfg.region_size) { return -1; }

    if (off + len <= s_cfg.dir_bytes)
    {
        /* fully inside the directory cache */
        memcpy(buf, s_cfg.dir_cache + off, len);
        return 0;
    }
    if (off >= s_cfg.dir_bytes)
    {
        /* fully inside the data region */
        return s_cfg.read(s_cfg.base_addr + off, buf, len);
    }

    /* crosses the boundary: split in two */
    uint32_t cache_part  = s_cfg.dir_bytes - off;
    memcpy(buf, s_cfg.dir_cache + off, cache_part);
    return s_cfg.read(s_cfg.base_addr + s_cfg.dir_bytes,
                      (uint8_t *)buf + cache_part, len - cache_part);
}

/* Direct data-region write: program is only valid where the target bytes are
 * currently 0xFF (erased). This is the normal case for the file_db append
 * path - the allocator has already ensured we never write over occupied
 * bytes. A non-erased target means an upper-layer logic error. */
static int program_data_region(uint32_t off, const void *buf, uint32_t len)
{
    /* Split by page to fit most SPI NOR program limits. */
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0)
    {
        uint32_t page_off = off % s_cfg.page_size;
        uint32_t avail    = s_cfg.page_size - page_off;
        uint32_t n        = (len > avail) ? avail : len;
        if (s_cfg.program(s_cfg.base_addr + off, p, n) != 0) { return -1; }
        off += n;
        p   += n;
        len -= n;
    }
    return 0;
}

static int nor_write(uint32_t off, const void *buf, uint32_t len)
{
    if (off + len < off || off + len > s_cfg.region_size) { return -1; }

    /* Write into the directory cache: mark the touched sectors dirty, then
     * flush immediately. Each file_db directory write touches only one
     * sector; for simplicity we flush per sector right away. To batch them
     * (fewer erases, higher performance), move the flush_dir_sector calls
     * below into an explicit fdb_nor_flush() call. */
    if (off + len <= s_cfg.dir_bytes)
    {
        memcpy(s_cfg.dir_cache + off, buf, len);
        uint32_t s0 = off / s_cfg.sector_size;
        uint32_t s1 = (off + len - 1u) / s_cfg.sector_size;
        for (uint32_t s = s0; s <= s1; ++s) { dirty_set(s); }
        for (uint32_t s = s0; s <= s1; ++s)
        {
            if (flush_dir_sector(s) != 0) { return -1; }
        }
        return 0;
    }

    if (off >= s_cfg.dir_bytes)
    {
        return program_data_region(off, buf, len);
    }

    /* Write crossing the boundary: split it. */
    uint32_t cache_part = s_cfg.dir_bytes - off;
    memcpy(s_cfg.dir_cache + off, buf, cache_part);
    uint32_t s0 = off / s_cfg.sector_size;
    uint32_t s1 = (s_cfg.dir_bytes - 1u) / s_cfg.sector_size;
    for (uint32_t s = s0; s <= s1; ++s) { dirty_set(s); }
    for (uint32_t s = s0; s <= s1; ++s)
    {
        if (flush_dir_sector(s) != 0) { return -1; }
    }
    return program_data_region(s_cfg.dir_bytes,
                               (const uint8_t *)buf + cache_part,
                               len - cache_part);
}

/* Used only by fdb_format() to wipe the whole region once. Erases by sector. */
static int nor_erase(uint32_t off, uint32_t len)
{
    if (off + len < off || off + len > s_cfg.region_size) { return -1; }
    if ((off  & (s_cfg.sector_size - 1u)) != 0 ||
        (len  & (s_cfg.sector_size - 1u)) != 0)
    {
        /* file_db only calls erase(0, total) at format time, which is always
         * sector aligned. Unaligned erase is not supported - reject it here. */
        return -1;
    }

    uint32_t end = off + len;
    for (uint32_t a = off; a < end; a += s_cfg.sector_size)
    {
        if (s_cfg.erase_sector(s_cfg.base_addr + a) != 0) { return -1; }
    }
    /* Also set the affected span of the directory cache to 0xFF and clear
     * its dirty bits, keeping cache and flash in sync. */
    if (off < s_cfg.dir_bytes)
    {
        uint32_t lo = off;
        uint32_t hi = (end < s_cfg.dir_bytes) ? end : s_cfg.dir_bytes;
        memset(s_cfg.dir_cache + lo, 0xFF, hi - lo);
        uint32_t s0 = lo / s_cfg.sector_size;
        uint32_t s1 = (hi - 1u) / s_cfg.sector_size;
        for (uint32_t s = s0; s <= s1; ++s) { dirty_clear(s); }
    }
    return 0;
}

static uint32_t nor_size(void)
{
    return s_cfg.region_size;
}

static uintptr_t nor_base_addr(void)
{
    return (uintptr_t)s_cfg.base_addr;
}

static const fdb_port_ops_t s_nor_ops =
{
    .init      = nor_init,
    .deinit    = nor_deinit,
    .read      = nor_read,
    .write     = nor_write,
    .erase     = nor_erase,
    .size      = nor_size,
    .base_addr = nor_base_addr,
};

/*============================================================================*
 *                              Public entry
 *============================================================================*/

/* Inject the HAL and region config. Call before fdb_init(). */
int fdb_port_nor_setup(const fdb_nor_cfg_t *cfg)
{
    if (!cfg) { return -1; }
    s_cfg         = *cfg;
    s_initialised = 1;
    return 0;
}

/* Force dirty directory sectors back to Flash (a sync point for deferred-flush mode). */
int fdb_port_nor_flush(void)
{
    return flush_all_dirty_sectors();
}

const fdb_port_ops_t *fdb_port_nor_get_ops(void)
{
    return &s_nor_ops;
}
