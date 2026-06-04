/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * Debug / introspection helpers. Output goes through FDB_LOG so it can
 * be redirected or compiled out via file_db_config.h.
 */
#include <string.h>
#include "file_db_internal.h"

static const char *state_name(uint8_t s)
{
    switch (s)
    {
    case FDB_ENTRY_FREE:    return "FREE   ";
    case FDB_ENTRY_WRITING: return "WRITING";
    case FDB_ENTRY_VALID:   return "VALID  ";
    case FDB_ENTRY_DELETED: return "DELETED";
    default:                return "??     ";
    }
}

void fdb_dump_super(void)
{
    if (!fdb_is_mounted()) { FDB_LOG("super: <not mounted>"); return; }
    const fdb_super_t *s = &fdb_ctx()->super;
    FDB_LOG("super: magic=0x%08X ver=%u entry_size=%u max_entries=%u "
            "dir@0x%X data@0x%X total=0x%X",
            (unsigned)s->magic, s->version, s->entry_size, (unsigned)s->max_entries,
            (unsigned)s->dir_offset, (unsigned)s->data_offset, (unsigned)s->total_size);
}

void fdb_dump_dir(void)
{
    if (!fdb_is_mounted()) { FDB_LOG("dir: <not mounted>"); return; }
    fdb_ctx_t *ctx = fdb_ctx();
    FDB_LOG("dir: %u entries", (unsigned)ctx->super.max_entries);
    for (uint16_t i = 0; i < ctx->super.max_entries; ++i)
    {
        const fdb_dir_entry_t *e = &ctx->dir[i];
        if (e->state == FDB_ENTRY_FREE)
        {
            /* Skip noisy FREE rows; comment out the continue to dump them too. */
            continue;
        }
        char crcbuf[12] = "--";
        if (e->flags & FDB_FLAG_HAS_DATA_CRC)
        {
            /* "0x%08X" formatting kept short; printf supports it on this target. */
            snprintf(crcbuf, sizeof(crcbuf), "0x%08X", (unsigned)e->data_crc);
        }
        FDB_LOG("  #%02u %s id=0x%08X off=0x%06X cap=0x%06X size=0x%06X "
                "exp=0x%06X flags=0x%02X data_crc=%s",
                i, state_name(e->state),
                (unsigned)e->file_id,
                (unsigned)e->offset,
                (unsigned)e->capacity,
                (unsigned)e->size,
                (unsigned)e->expected_size,
                e->flags,
                crcbuf);
    }
}

/* Walk occupied (VALID + WRITING) ranges in offset order, summarise free
 * gaps and the largest contiguous hole. Mirrors fdb_alloc_tail() logic. */
void fdb_dump_usage(void)
{
    if (!fdb_is_mounted()) { FDB_LOG("usage: <not mounted>"); return; }

    fdb_ctx_t *ctx = fdb_ctx();
    typedef struct { uint32_t off, end; } range_t;
    range_t occ[FDB_MAX_ENTRIES];
    uint16_t n = 0;

    for (uint16_t i = 0; i < ctx->super.max_entries; ++i)
    {
        const fdb_dir_entry_t *e = &ctx->dir[i];
        if (e->state != FDB_ENTRY_VALID && e->state != FDB_ENTRY_WRITING) { continue; }
        range_t r = { e->offset, e->offset + e->capacity };
        uint16_t j = n;
        while (j > 0 && occ[j - 1].off > r.off) { occ[j] = occ[j - 1]; --j; }
        occ[j] = r;
        ++n;
    }

    uint32_t total      = ctx->super.total_size - ctx->super.data_offset;
    uint32_t used       = 0;
    uint32_t largest    = 0;
    uint32_t cursor     = ctx->super.data_offset;

    FDB_LOG("usage: data region 0x%X..0x%X (%u bytes), %u occupied range(s)",
            (unsigned)ctx->super.data_offset,
            (unsigned)ctx->super.total_size,
            (unsigned)total, n);

    for (uint16_t i = 0; i < n; ++i)
    {
        if (occ[i].off > cursor)
        {
            uint32_t gap = occ[i].off - cursor;
            if (gap > largest) { largest = gap; }
            FDB_LOG("  free  0x%06X..0x%06X (%u)",
                    (unsigned)cursor, (unsigned)occ[i].off, (unsigned)gap);
        }
        FDB_LOG("  used  0x%06X..0x%06X (%u)",
                (unsigned)occ[i].off, (unsigned)occ[i].end,
                (unsigned)(occ[i].end - occ[i].off));
        used += occ[i].end - occ[i].off;
        if (occ[i].end > cursor) { cursor = occ[i].end; }
    }
    if (cursor < ctx->super.total_size)
    {
        uint32_t gap = ctx->super.total_size - cursor;
        if (gap > largest) { largest = gap; }
        FDB_LOG("  free  0x%06X..0x%06X (%u)",
                (unsigned)cursor, (unsigned)ctx->super.total_size, (unsigned)gap);
    }
    FDB_LOG("  total used=%u free=%u largest_hole=%u",
            (unsigned)used, (unsigned)(total - used), (unsigned)largest);
}

void fdb_dump_file(uint32_t file_id, uint32_t head_n)
{
    if (!fdb_is_mounted()) { FDB_LOG("file: <not mounted>"); return; }

    fdb_file_info_t info;
    int rc = fdb_stat(file_id, &info);
    if (rc != FDB_OK)
    {
        FDB_LOG("file: id=0x%08X not found (%d)", (unsigned)file_id, rc);
        return;
    }

    FDB_LOG("file: id=0x%08X off=0x%X size=%u cap=%u flags=0x%02X%s",
            (unsigned)info.file_id, (unsigned)info.offset,
            (unsigned)info.size, (unsigned)info.capacity, info.flags,
            (info.flags & FDB_FLAG_HAS_DATA_CRC) ? " (has data crc)" : "");

    if (head_n == 0) { return; }
    if (head_n > info.size) { head_n = info.size; }

    uint8_t buf[FDB_LOG_HEX_PER_LINE];
    uint32_t off_in_file = 0;
    while (off_in_file < head_n)
    {
        uint32_t n = head_n - off_in_file;
        if (n > FDB_LOG_HEX_PER_LINE) { n = FDB_LOG_HEX_PER_LINE; }
        if (fdb_io_read(info.offset + off_in_file, buf, n) != FDB_OK)
        {
            FDB_LOG("  read error at +%u", (unsigned)off_in_file);
            return;
        }

        char hex[FDB_LOG_HEX_PER_LINE * 3 + 1];
        char asc[FDB_LOG_HEX_PER_LINE + 1];
        char *hp = hex;
        char *ap = asc;
        for (uint32_t i = 0; i < n; ++i)
        {
            static const char *digits = "0123456789ABCDEF";
            *hp++ = digits[(buf[i] >> 4) & 0xF];
            *hp++ = digits[buf[i] & 0xF];
            *hp++ = ' ';
            *ap++ = (buf[i] >= 0x20 && buf[i] < 0x7F) ? (char)buf[i] : '.';
        }
        *hp = '\0';
        *ap = '\0';
        FDB_LOG("  %06X  %-*s |%s|",
                (unsigned)(info.offset + off_in_file),
                FDB_LOG_HEX_PER_LINE * 3, hex, asc);
        off_in_file += n;
    }
}
