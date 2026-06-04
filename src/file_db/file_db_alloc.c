/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * First-fit allocator over the data region.
 *
 * Occupied intervals come from VALID and WRITING entries (a freshly created
 * but not-yet-committed file must hold its space). DELETED entries are
 * tombstones and their range is reclaimable. Mount has already reaped any
 * WRITING entries left over from a previous run, so during normal operation
 * the only WRITING entries present are ones the current process owns.
 *
 * No free-list cache is maintained; the directory is rescanned on each
 * fdb_alloc(). With FDB_MAX_ENTRIES on the order of 32-128 this is trivial.
 */
#include <string.h>
#include "file_db_internal.h"

uint32_t fdb_align_up(uint32_t v, uint32_t a)
{
    return (v + (a - 1u)) & ~(a - 1u);
}

typedef struct
{
    uint32_t off;
    uint32_t end;       /* off + capacity */
} fdb_range_t;

/* Collect occupied ranges (VALID + WRITING) and insertion-sort by offset. */
static uint16_t collect_occupied(fdb_range_t *ranges, uint16_t cap)
{
    fdb_ctx_t *ctx = fdb_ctx();
    uint16_t n = 0;

    for (uint16_t i = 0; i < ctx->super.max_entries && n < cap; ++i)
    {
        const fdb_dir_entry_t *e = &ctx->dir[i];
        if (e->state != FDB_ENTRY_VALID && e->state != FDB_ENTRY_WRITING)
        {
            continue;
        }
        fdb_range_t r = { e->offset, e->offset + e->capacity };

        /* insertion sort */
        uint16_t j = n;
        while (j > 0 && ranges[j - 1].off > r.off)
        {
            ranges[j] = ranges[j - 1];
            --j;
        }
        ranges[j] = r;
        ++n;
    }
    return n;
}

int fdb_alloc(uint32_t need_aligned, uint32_t *out_offset)
{
    if (need_aligned == 0 || !out_offset)
    {
        return FDB_ERR_INVALID_PARAM;
    }

    fdb_ctx_t *ctx = fdb_ctx();
    fdb_range_t occ[FDB_MAX_ENTRIES];
    uint16_t n = collect_occupied(occ, FDB_MAX_ENTRIES);

    uint32_t cursor = ctx->super.data_offset;
    uint32_t total  = ctx->super.total_size;

    for (uint16_t i = 0; i < n; ++i)
    {
        if (occ[i].off >= cursor && (occ[i].off - cursor) >= need_aligned)
        {
            *out_offset = cursor;
            return FDB_OK;
        }
        if (occ[i].end > cursor)
        {
            cursor = occ[i].end;
        }
    }

    if (cursor < total && (total - cursor) >= need_aligned)
    {
        *out_offset = cursor;
        return FDB_OK;
    }
    return FDB_ERR_NO_SPACE;
}

/* Tail allocation for streaming with unknown size. The capacity we can
 * grow into is the contiguous space from the tail of the last occupant
 * to the end of the region. */
int fdb_alloc_tail(uint32_t *out_offset, uint32_t *out_max_capacity)
{
    if (!out_offset || !out_max_capacity)
    {
        return FDB_ERR_INVALID_PARAM;
    }

    fdb_ctx_t *ctx = fdb_ctx();
    fdb_range_t occ[FDB_MAX_ENTRIES];
    uint16_t n = collect_occupied(occ, FDB_MAX_ENTRIES);

    uint32_t cursor = ctx->super.data_offset;
    for (uint16_t i = 0; i < n; ++i)
    {
        if (occ[i].end > cursor) { cursor = occ[i].end; }
    }

    if (cursor >= ctx->super.total_size)
    {
        return FDB_ERR_NO_SPACE;
    }

    cursor = fdb_align_up(cursor, FDB_DATA_ALIGN);
    if (cursor >= ctx->super.total_size)
    {
        return FDB_ERR_NO_SPACE;
    }

    *out_offset       = cursor;
    *out_max_capacity = ctx->super.total_size - cursor;
    return FDB_OK;
}
