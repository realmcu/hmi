/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * file_db --- a tiny filesystem-like store for chunked file uploads.
 *
 * On-storage layout
 * -----------------
 *   offset 0
 *     +-------------------------+
 *     | superblock              |  magic, version, dir/data offsets, crc
 *     +-------------------------+
 *     | directory table         |  FDB_MAX_ENTRIES * fdb_dir_entry_t
 *     +-------------------------+ <-- data_offset
 *     | data region             |
 *     +-------------------------+ <-- total_size
 *
 * Commit protocol (interrupt-safe)
 * --------------------------------
 *   create   : pick a FREE slot, allocate space, write entry as WRITING.
 *   append   : write data into the reserved region; do NOT touch the
 *              directory until commit. The in-RAM size grows.
 *   commit   : write the entry as VALID with the final size. The state
 *              transition is the durability point.
 *
 * Recovery
 * --------
 *   Mount inspects every entry. WRITING -> rewritten as FREE (interrupted
 *   write). entry_crc mismatch -> rewritten as FREE (torn directory write).
 *   DELETED -> kept as a tombstone, its range is reclaimable.
 *
 * Integrity
 * ---------
 *   The superblock and every directory entry carry a CRC32 over their own
 *   metadata. End-to-end data integrity is the caller's responsibility:
 *   they may pass a CRC to fdb_commit_crc() which will be verifiable
 *   later via fdb_verify(). Without a data CRC the module guarantees
 *   only that the file's directory metadata is intact and that a partial
 *   write cannot masquerade as a complete file.
 */
#include <string.h>
#include "file_db_internal.h"

static fdb_ctx_t s_ctx;

fdb_ctx_t *fdb_ctx(void)
{
    return &s_ctx;
}

/*============================================================================*
 *                              Helpers
 *============================================================================*/

static inline bool ctx_mounted(void)
{
    return s_ctx.mounted;
}

static fdb_file_t *handle_alloc(void)
{
    for (int i = 0; i < FDB_MAX_OPEN_HANDLES; ++i)
    {
        if (!s_ctx.handles[i].in_use)
        {
            memset(&s_ctx.handles[i], 0, sizeof(s_ctx.handles[i]));
            s_ctx.handles[i].in_use = true;
            return &s_ctx.handles[i];
        }
    }
    return NULL;
}

static void handle_free(fdb_file_t *h)
{
    if (h) { memset(h, 0, sizeof(*h)); }
}

static int handle_owned(const fdb_file_t *h)
{
    if (!h) { return 0; }
    return (h >= &s_ctx.handles[0]) &&
           (h <  &s_ctx.handles[FDB_MAX_OPEN_HANDLES]) &&
           h->in_use;
}

static int id_in_use(uint32_t file_id)
{
    /* In-use means a non-FREE / non-DELETED entry holds this id. */
    for (uint16_t i = 0; i < s_ctx.super.max_entries; ++i)
    {
        const fdb_dir_entry_t *e = &s_ctx.dir[i];
        if ((e->state == FDB_ENTRY_VALID || e->state == FDB_ENTRY_WRITING) &&
            e->file_id == file_id)
        {
            return 1;
        }
    }
    return 0;
}

static int reclaim_slot(uint16_t slot)
{
    fdb_dir_entry_t e;
    memset(&e, 0, sizeof(e));
    e.state     = FDB_ENTRY_FREE;
    e.entry_crc = fdb_dir_entry_calc_crc(&e);
    return fdb_dir_write_entry(slot, &e);
}

/*============================================================================*
 *                              Lifecycle
 *============================================================================*/

int fdb_init(const fdb_port_ops_t *ops)
{
    if (!ops || !ops->read || !ops->write || !ops->size)
    {
        return FDB_ERR_INVALID_PARAM;
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.ops = ops;
    if (ops->init)
    {
        int rc = ops->init();
        if (rc != 0) { FDB_LOG("ops->init %d", rc); return FDB_ERR_IO; }
    }
    return FDB_OK;
}

int fdb_deinit(void)
{
    for (int i = 0; i < FDB_MAX_OPEN_HANDLES; ++i)
    {
        s_ctx.handles[i].in_use = false;
    }
    if (s_ctx.ops && s_ctx.ops->deinit)
    {
        s_ctx.ops->deinit();
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    return FDB_OK;
}

bool fdb_is_mounted(void)
{
    return ctx_mounted();
}

int fdb_format(void)
{
    if (!s_ctx.ops) { return FDB_ERR_NOT_MOUNTED; }

    uint32_t total = s_ctx.ops->size();
    uint32_t dir_off  = fdb_align_up(sizeof(fdb_super_t), 8u);
    uint32_t dir_sz   = (uint32_t)FDB_MAX_ENTRIES * sizeof(fdb_dir_entry_t);
    uint32_t data_off = fdb_align_up(dir_off + dir_sz, FDB_DATA_ALIGN);

    if (data_off >= total)
    {
        return FDB_ERR_NO_SPACE;
    }

    fdb_io_erase(0, total);  /* best-effort; RAM port may no-op */

    fdb_super_t s;
    memset(&s, 0, sizeof(s));
    s.magic       = FDB_SUPER_MAGIC;
    s.version     = FDB_SUPER_VERSION;
    s.entry_size  = (uint16_t)sizeof(fdb_dir_entry_t);
    s.max_entries = FDB_MAX_ENTRIES;
    s.dir_offset  = dir_off;
    s.data_offset = data_off;
    s.total_size  = total;
    s.crc         = fdb_super_calc_crc(&s);

    s_ctx.super   = s;
    int rc = fdb_super_write(&s);
    if (rc != FDB_OK) { FDB_LOG("fdb_super_write %d", rc); return rc; }

    /* Lay down FDB_MAX_ENTRIES FREE entries with valid crcs. */
    fdb_dir_entry_t e;
    memset(&e, 0, sizeof(e));
    e.state     = FDB_ENTRY_FREE;
    e.entry_crc = fdb_dir_entry_calc_crc(&e);

    for (uint16_t i = 0; i < FDB_MAX_ENTRIES; ++i)
    {
        rc = fdb_dir_write_entry(i, &e);
        if (rc != FDB_OK) { FDB_LOG("fdb_dir_write_entry %d", rc); return rc; }
    }

    s_ctx.mounted = true;
    return FDB_OK;
}

int fdb_mount(void)
{
    if (!s_ctx.ops) { return FDB_ERR_NOT_MOUNTED; }

    fdb_super_t s;
    int rc = fdb_super_read(&s);
    if (rc != FDB_OK) { return rc; }

    if (s.magic != FDB_SUPER_MAGIC ||
        s.version != FDB_SUPER_VERSION ||
        s.entry_size != sizeof(fdb_dir_entry_t) ||
        s.max_entries > FDB_MAX_ENTRIES ||
        s.crc != fdb_super_calc_crc(&s))
    {
        return FDB_ERR_CORRUPT;
    }
    s_ctx.super = s;

    /* Walk every slot. */
    for (uint16_t i = 0; i < s.max_entries; ++i)
    {
        fdb_dir_entry_t e;
        rc = fdb_dir_read_entry(i, &e);
        if (rc != FDB_OK) { return rc; }

        bool reap            = false;
        bool finish_delete   = false;
        if (e.entry_crc != fdb_dir_entry_calc_crc(&e))
        {
            FDB_LOG("mount: slot %u crc mismatch -> reaped", i);
            reap = true;
        }
        else if (e.state == FDB_ENTRY_WRITING)
        {
            FDB_LOG("mount: slot %u was WRITING (id=0x%08X) -> reaped",
                    i, (unsigned)e.file_id);
            reap = true;
        }
        else if (e.state == FDB_ENTRY_DELETED)
        {
            /* Pending physical delete - complete it now. */
            FDB_LOG("mount: slot %u DELETED in-flight (id=0x%08X) -> erase+reap",
                    i, (unsigned)e.file_id);
            finish_delete = true;
        }
        else if (e.state != FDB_ENTRY_FREE &&
                 e.state != FDB_ENTRY_VALID)
        {
            FDB_LOG("mount: slot %u unknown state 0x%02X -> reaped", i, e.state);
            reap = true;
        }
        else
        {
            /* Range sanity: occupant must lie inside data region. */
            if (e.state == FDB_ENTRY_VALID)
            {
                if (e.offset < s.data_offset ||
                    e.offset + e.capacity > s.total_size ||
                    e.offset + e.capacity < e.offset)
                {
                    FDB_LOG("mount: slot %u out-of-range -> reaped", i);
                    reap = true;
                }
            }
        }

        if (finish_delete)
        {
            /* Best-effort. Keep the entry as DELETED if erase fails so a
             * future mount can retry; the allocator already treats DELETED
             * as free space. */
            (void)fdb_io_erase(e.offset, e.capacity);
            s_ctx.dir[i] = e;
            rc = reclaim_slot(i);
            if (rc != FDB_OK)
            {
                FDB_LOG("mount: slot %u reclaim failed (%d), leaving DELETED", i, rc);
            }
        }
        else if (reap)
        {
            rc = reclaim_slot(i);
            if (rc != FDB_OK) { return rc; }
        }
        else
        {
            s_ctx.dir[i] = e;
        }
    }

    s_ctx.mounted = true;
    return FDB_OK;
}

/*============================================================================*
 *                              Streaming write
 *============================================================================*/

int fdb_create(uint32_t file_id, uint32_t max_size, uint32_t expected_size,
               fdb_file_t **out)
{
    if (!ctx_mounted())   { return FDB_ERR_NOT_MOUNTED; }
    if (!out)             { return FDB_ERR_INVALID_PARAM; }
    if (id_in_use(file_id)) { return FDB_ERR_EXISTS; }

    uint16_t slot;
    int rc = fdb_find_free_slot(&slot);
    if (rc != FDB_OK) { return rc; }

    uint32_t offset = 0;
    uint32_t cap;
    uint8_t  flags = 0;

    if (max_size > 0)
    {
        cap = fdb_align_up(max_size, FDB_DATA_ALIGN);
        rc  = fdb_alloc(cap, &offset);
    }
    else
    {
        rc = fdb_alloc_tail(&offset, &cap);
    }
    if (rc != FDB_OK) { return rc; }

    if (expected_size > 0)
    {
        if (expected_size > cap) { return FDB_ERR_NO_SPACE; }
        flags |= FDB_FLAG_SIZE_KNOWN;
    }

    fdb_file_t *h = handle_alloc();
    if (!h) { return FDB_ERR_TOO_MANY_OPEN; }

    h->writing       = true;
    h->slot          = slot;
    h->file_id       = file_id;
    h->offset        = offset;
    h->capacity      = cap;
    h->size          = 0;
    h->expected_size = expected_size;
    h->flags         = flags;

    fdb_dir_entry_t e;
    memset(&e, 0, sizeof(e));
    e.state         = FDB_ENTRY_WRITING;
    e.flags         = flags;
    e.file_id       = file_id;
    e.offset        = offset;
    e.capacity      = cap;
    e.expected_size = expected_size;
    e.size          = 0;
    e.data_crc      = 0;
    e.entry_crc     = fdb_dir_entry_calc_crc(&e);

    rc = fdb_dir_write_entry(slot, &e);
    if (rc != FDB_OK)
    {
        handle_free(h);
        return rc;
    }

    *out = h;
    return FDB_OK;
}

int fdb_append(fdb_file_t *f, const void *data, uint32_t len)
{
    if (!ctx_mounted())            { return FDB_ERR_NOT_MOUNTED; }
    if (!handle_owned(f) || !f->writing) { return FDB_ERR_STATE; }
    if (!data || len == 0)         { return FDB_ERR_INVALID_PARAM; }
    if (len > f->capacity - f->size) { return FDB_ERR_NO_SPACE; }

    int rc = fdb_io_write(f->offset + f->size, data, len);
    if (rc != FDB_OK) { return rc; }

    f->size += len;
    return FDB_OK;
}

static int commit_internal(fdb_file_t *f, bool with_crc, uint32_t crc)
{
    if (!ctx_mounted())                  { return FDB_ERR_NOT_MOUNTED; }
    if (!handle_owned(f) || !f->writing) { return FDB_ERR_STATE; }

    if ((f->flags & FDB_FLAG_SIZE_KNOWN) && f->size != f->expected_size)
    {
        return FDB_ERR_STATE;
    }

    fdb_dir_entry_t e = s_ctx.dir[f->slot];
    e.state    = FDB_ENTRY_VALID;
    e.size     = f->size;
    /* Shrink the reservation to the actual occupied region. Releases any
     * over-reservation (especially the entire tail in max_size=0 mode) for
     * later first-fit allocations. */
    {
        uint32_t shrunk = fdb_align_up(f->size, FDB_DATA_ALIGN);
        if (shrunk == 0)        { shrunk = FDB_DATA_ALIGN; }
        if (shrunk < e.capacity) { e.capacity = shrunk; f->capacity = shrunk; }
    }
    if (with_crc)
    {
        e.flags    |= FDB_FLAG_HAS_DATA_CRC;
        e.data_crc  = crc;
    }
    e.entry_crc = fdb_dir_entry_calc_crc(&e);

    int rc = fdb_dir_write_entry(f->slot, &e);
    if (rc != FDB_OK) { return rc; }

    f->writing = false;
    /* keep the handle around for read; caller must fdb_close() */
    return FDB_OK;
}

int fdb_commit(fdb_file_t *f)
{
    return commit_internal(f, false, 0);
}

int fdb_commit_crc(fdb_file_t *f, uint32_t data_crc)
{
    return commit_internal(f, true, data_crc);
}

int fdb_abort(fdb_file_t *f)
{
    if (!ctx_mounted())                  { return FDB_ERR_NOT_MOUNTED; }
    if (!handle_owned(f) || !f->writing) { return FDB_ERR_STATE; }

    int rc = reclaim_slot(f->slot);
    handle_free(f);
    return rc;
}

/*============================================================================*
 *                              Read / metadata
 *============================================================================*/

int fdb_open(uint32_t file_id, fdb_file_t **out)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }
    if (!out)           { return FDB_ERR_INVALID_PARAM; }

    uint16_t slot;
    int rc = fdb_find_slot_by_id(file_id, FDB_ENTRY_VALID, &slot);
    if (rc != FDB_OK) { return rc; }

    fdb_file_t *h = handle_alloc();
    if (!h) { return FDB_ERR_TOO_MANY_OPEN; }

    const fdb_dir_entry_t *e = &s_ctx.dir[slot];
    h->writing       = false;
    h->slot          = slot;
    h->file_id       = file_id;
    h->offset        = e->offset;
    h->capacity      = e->capacity;
    h->size          = e->size;
    h->expected_size = e->expected_size;
    h->flags         = e->flags;

    *out = h;
    return FDB_OK;
}

int fdb_read(fdb_file_t *f, uint32_t offset, void *buf, uint32_t len)
{
    if (!ctx_mounted())          { return FDB_ERR_NOT_MOUNTED; }
    if (!handle_owned(f))        { return FDB_ERR_STATE; }
    if (f->writing)              { return FDB_ERR_STATE; }
    if (!buf || len == 0)        { return FDB_ERR_INVALID_PARAM; }
    if (offset >= f->size)       { return FDB_ERR_INVALID_PARAM; }
    if (len > f->size - offset)  { len = f->size - offset; }

    return fdb_io_read(f->offset + offset, buf, len);
}

int fdb_close(fdb_file_t *f)
{
    if (!handle_owned(f)) { return FDB_ERR_STATE; }
    if (f->writing)
    {
        /* Closing a still-writing handle is treated as abort. */
        return fdb_abort(f);
    }
    handle_free(f);
    return FDB_OK;
}

/* Three-step physical delete (scheme A):
 *   1. Mark the directory entry DELETED (intent recorded).
 *   2. Erase the file's data sectors via the port.
 *   3. Release the slot (rewrite as FREE).
 *
 * If power is lost between steps 1 and 3, the next mount sees a DELETED
 * entry and finishes the job - erasing the data range (idempotent on
 * already-erased sectors) and releasing the slot. */
int fdb_delete(uint32_t file_id)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }

    uint16_t slot;
    int rc = fdb_find_slot_by_id(file_id, FDB_ENTRY_VALID, &slot);
    if (rc != FDB_OK) { return rc; }

    /* Refuse if any open handle still owns this slot. */
    for (int i = 0; i < FDB_MAX_OPEN_HANDLES; ++i)
    {
        if (s_ctx.handles[i].in_use && s_ctx.handles[i].slot == slot)
        {
            return FDB_ERR_BUSY;
        }
    }

    fdb_dir_entry_t e        = s_ctx.dir[slot];
    uint32_t        data_off = e.offset;
    uint32_t        data_cap = e.capacity;

    /* Step 1: record intent. */
    e.state     = FDB_ENTRY_DELETED;
    e.entry_crc = fdb_dir_entry_calc_crc(&e);
    rc = fdb_dir_write_entry(slot, &e);
    if (rc != FDB_OK) { return rc; }

    /* Step 2: physical erase of the data range. */
    rc = fdb_io_erase(data_off, data_cap);
    if (rc != FDB_OK)
    {
        /* Leave the slot DELETED; mount will retry. The slot's space is
         * still considered free by the allocator, so functionally the
         * file is gone - just not yet erased on the medium. */
        return rc;
    }

    /* Step 3: release the slot. */
    return reclaim_slot(slot);
}

int fdb_delete_by_addr(uintptr_t addr)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }
    if (!s_ctx.ops || !s_ctx.ops->base_addr) { return FDB_ERR_UNSUPPORTED; }

    uintptr_t base = s_ctx.ops->base_addr();
    if (addr < base) { return FDB_ERR_NOT_FOUND; }
    uintptr_t off = addr - base;

    for (uint16_t i = 0; i < s_ctx.super.max_entries; ++i)
    {
        const fdb_dir_entry_t *e = &s_ctx.dir[i];
        if (e->state != FDB_ENTRY_VALID) { continue; }
        if (off >= (uintptr_t)e->offset &&
            off < (uintptr_t)e->offset + (uintptr_t)e->capacity)
        {
            return fdb_delete(e->file_id);
        }
    }
    return FDB_ERR_NOT_FOUND;
}

int fdb_exists(uint32_t file_id)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }
    uint16_t slot;
    return fdb_find_slot_by_id(file_id, FDB_ENTRY_VALID, &slot);
}

static void entry_to_info(const fdb_dir_entry_t *e, fdb_file_info_t *out)
{
    out->file_id       = e->file_id;
    out->offset        = e->offset;
    out->capacity      = e->capacity;
    out->size          = e->size;
    out->expected_size = e->expected_size;
    out->data_crc      = e->data_crc;
    out->flags         = e->flags;
    out->state         = e->state;
}

int fdb_stat(uint32_t file_id, fdb_file_info_t *out)
{
    if (!ctx_mounted())  { return FDB_ERR_NOT_MOUNTED; }
    if (!out)            { return FDB_ERR_INVALID_PARAM; }
    uint16_t slot;
    int rc = fdb_find_slot_by_id(file_id, FDB_ENTRY_VALID, &slot);
    if (rc != FDB_OK) { return rc; }
    entry_to_info(&s_ctx.dir[slot], out);
    return FDB_OK;
}

int fdb_foreach(fdb_iter_cb_t cb, void *user)
{
    if (!ctx_mounted())  { return FDB_ERR_NOT_MOUNTED; }
    if (!cb)             { return FDB_ERR_INVALID_PARAM; }

    fdb_file_info_t info;
    for (uint16_t i = 0; i < s_ctx.super.max_entries; ++i)
    {
        if (s_ctx.dir[i].state != FDB_ENTRY_VALID) { continue; }
        entry_to_info(&s_ctx.dir[i], &info);
        if (cb(&info, user) != 0) { break; }
    }
    return FDB_OK;
}

int fdb_get_file_count(uint32_t *out)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }
    if (!out)           { return FDB_ERR_INVALID_PARAM; }

    uint32_t n = 0;
    for (uint16_t i = 0; i < s_ctx.super.max_entries; ++i)
    {
        if (s_ctx.dir[i].state == FDB_ENTRY_VALID) { ++n; }
    }
    *out = n;
    return FDB_OK;
}

uint32_t fdb_file_count(void)
{
    if (!ctx_mounted()) { return 0; }
    uint32_t n = 0;
    for (uint16_t i = 0; i < s_ctx.super.max_entries; ++i)
    {
        if (s_ctx.dir[i].state == FDB_ENTRY_VALID) { ++n; }
    }
    return n;
}

int fdb_get_addr_by_id(uint32_t file_id, uintptr_t *out_addr,
                       uint32_t *out_size)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }
    if (!out_addr)      { return FDB_ERR_INVALID_PARAM; }
    if (!s_ctx.ops || !s_ctx.ops->base_addr) { return FDB_ERR_UNSUPPORTED; }

    uint16_t slot;
    int rc = fdb_find_slot_by_id(file_id, FDB_ENTRY_VALID, &slot);
    if (rc != FDB_OK) { return rc; }

    const fdb_dir_entry_t *e = &s_ctx.dir[slot];
    *out_addr = s_ctx.ops->base_addr() + (uintptr_t)e->offset;
    if (out_size) { *out_size = e->size; }
    return FDB_OK;
}

int fdb_get_file_addr(uint32_t index, uintptr_t *out_addr,
                      uint32_t *out_size, uint32_t *out_id)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }
    if (!out_addr)      { return FDB_ERR_INVALID_PARAM; }
    if (!s_ctx.ops || !s_ctx.ops->base_addr) { return FDB_ERR_UNSUPPORTED; }

    uint32_t seen = 0;
    for (uint16_t i = 0; i < s_ctx.super.max_entries; ++i)
    {
        const fdb_dir_entry_t *e = &s_ctx.dir[i];
        if (e->state != FDB_ENTRY_VALID) { continue; }
        if (seen == index)
        {
            *out_addr = s_ctx.ops->base_addr() + (uintptr_t)e->offset;
            if (out_size) { *out_size = e->size; }
            if (out_id)   { *out_id   = e->file_id; }
            return FDB_OK;
        }
        ++seen;
    }
    return FDB_ERR_NOT_FOUND;
}

int fdb_get_file_list(fdb_file_info_t *out, uint32_t max, uint32_t *count_out)
{
    if (!ctx_mounted())              { return FDB_ERR_NOT_MOUNTED; }
    if (!out || max == 0 || !count_out) { return FDB_ERR_INVALID_PARAM; }

    uint32_t n = 0;
    for (uint16_t i = 0; i < s_ctx.super.max_entries && n < max; ++i)
    {
        if (s_ctx.dir[i].state != FDB_ENTRY_VALID) { continue; }
        entry_to_info(&s_ctx.dir[i], &out[n]);
        ++n;
    }
    *count_out = n;
    return FDB_OK;
}

int fdb_get_dir_info(fdb_dir_info_t *out)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }
    if (!out)           { return FDB_ERR_INVALID_PARAM; }

    memset(out, 0, sizeof(*out));
    out->max_entries      = s_ctx.super.max_entries;
    out->data_region_size = s_ctx.super.total_size - s_ctx.super.data_offset;

    /* Collect occupied (VALID + WRITING) ranges and sort by offset; this
     * mirrors the allocator's view of "what is currently in the way". */
    typedef struct { uint32_t off, end; } range_t;
    range_t  occ[FDB_MAX_ENTRIES] = {0};   /* zero-init: only [0,n) is ever read,
                                              but silences cppcheck uninitvar */
    uint16_t n = 0;

    for (uint16_t i = 0; i < s_ctx.super.max_entries; ++i)
    {
        const fdb_dir_entry_t *e = &s_ctx.dir[i];
        if (e->state == FDB_ENTRY_FREE) { continue; }
        out->entries_used++;
        if (e->state == FDB_ENTRY_VALID)
        {
            out->file_count++;
            out->bytes_used     += e->size;
            out->bytes_reserved += e->capacity;
        }
        if (e->state == FDB_ENTRY_VALID || e->state == FDB_ENTRY_WRITING)
        {
            range_t r = { e->offset, e->offset + e->capacity };
            uint16_t j = n;
            while (j > 0 && occ[j - 1].off > r.off) { occ[j] = occ[j - 1]; --j; }
            occ[j] = r;
            ++n;
        }
    }

    uint32_t cursor  = s_ctx.super.data_offset;
    uint32_t largest = 0;
    uint32_t freebs  = 0;
    for (uint16_t i = 0; i < n; ++i)
    {
        if (occ[i].off > cursor)
        {
            uint32_t gap = occ[i].off - cursor;
            freebs += gap;
            if (gap > largest) { largest = gap; }
        }
        if (occ[i].end > cursor) { cursor = occ[i].end; }
    }
    if (cursor < s_ctx.super.total_size)
    {
        uint32_t gap = s_ctx.super.total_size - cursor;
        freebs += gap;
        if (gap > largest) { largest = gap; }
    }
    out->bytes_free        = freebs;
    out->largest_free_hole = largest;
    return FDB_OK;
}

int fdb_verify(uint32_t file_id)
{
    if (!ctx_mounted()) { return FDB_ERR_NOT_MOUNTED; }

    uint16_t slot;
    int rc = fdb_find_slot_by_id(file_id, FDB_ENTRY_VALID, &slot);
    if (rc != FDB_OK) { return rc; }

    const fdb_dir_entry_t *e = &s_ctx.dir[slot];
    if (!(e->flags & FDB_FLAG_HAS_DATA_CRC))
    {
        return FDB_ERR_UNSUPPORTED;
    }

    /* Stream the file through CRC32 in 64-byte chunks to avoid stack use. */
    uint8_t  chunk[64];
    uint32_t remaining = e->size;
    uint32_t off       = e->offset;
    uint32_t state     = fdb_crc32_init();

    while (remaining > 0)
    {
        uint32_t n = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
        rc = fdb_io_read(off, chunk, n);
        if (rc != FDB_OK) { return rc; }
        state      = fdb_crc32(state, chunk, n);
        off       += n;
        remaining -= n;
    }
    uint32_t got = fdb_crc32_finish(state);
    return (got == e->data_crc) ? FDB_OK : FDB_ERR_CORRUPT;
}
