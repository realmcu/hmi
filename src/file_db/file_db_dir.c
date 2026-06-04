/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * Directory and superblock persistence helpers.
 *
 * The CRC of an entry/superblock covers every byte up to but not including
 * the trailing crc field, so write order on flash media does not affect the
 * detection of torn writes (the last word is always the crc).
 */
#include <string.h>
#include "file_db_internal.h"


uint32_t fdb_dir_entry_off(uint16_t slot)
{
    return fdb_ctx()->super.dir_offset + (uint32_t)slot * sizeof(fdb_dir_entry_t);
}

uint32_t fdb_dir_entry_calc_crc(const fdb_dir_entry_t *e)
{
    uint32_t s = fdb_crc32_init();
    s = fdb_crc32(s, e, sizeof(*e) - sizeof(e->entry_crc));
    return fdb_crc32_finish(s);
}

uint32_t fdb_super_calc_crc(const fdb_super_t *s)
{
    uint32_t st = fdb_crc32_init();
    st = fdb_crc32(st, s, sizeof(*s) - sizeof(s->crc));
    return fdb_crc32_finish(st);
}

int fdb_dir_write_entry(uint16_t slot, const fdb_dir_entry_t *e)
{
    fdb_ctx_t *ctx = fdb_ctx();
    if (slot >= ctx->super.max_entries)
    {
        return FDB_ERR_INVALID_PARAM;
    }
    int rc = fdb_io_write(fdb_dir_entry_off(slot), e, sizeof(*e));
    if (rc == FDB_OK)
    {
        ctx->dir[slot] = *e;
    }
    return rc;
}

int fdb_dir_read_entry(uint16_t slot, fdb_dir_entry_t *out)
{
    fdb_ctx_t *ctx = fdb_ctx();
    if (slot >= ctx->super.max_entries)
    {
        return FDB_ERR_INVALID_PARAM;
    }
    return fdb_io_read(fdb_dir_entry_off(slot), out, sizeof(*out));
}

int fdb_super_write(const fdb_super_t *s)
{
    return fdb_io_write(0, s, sizeof(*s));
}

int fdb_super_read(fdb_super_t *out)
{
    return fdb_io_read(0, out, sizeof(*out));
}

int fdb_find_slot_by_id(uint32_t file_id, uint8_t state, uint16_t *out_slot)
{
    fdb_ctx_t *ctx = fdb_ctx();
    for (uint16_t i = 0; i < ctx->super.max_entries; ++i)
    {
        const fdb_dir_entry_t *e = &ctx->dir[i];
        if (e->state == state && e->file_id == file_id)
        {
            if (out_slot) { *out_slot = i; }
            return FDB_OK;
        }
    }
    return FDB_ERR_NOT_FOUND;
}

int fdb_find_free_slot(uint16_t *out_slot)
{
    fdb_ctx_t *ctx = fdb_ctx();
    for (uint16_t i = 0; i < ctx->super.max_entries; ++i)
    {
        if (ctx->dir[i].state == FDB_ENTRY_FREE)
        {
            if (out_slot) { *out_slot = i; }
            return FDB_OK;
        }
    }
    return FDB_ERR_DIR_FULL;
}

/* I/O wrappers add a coarse sanity check so a bad caller can not scribble
 * outside the mapped region. */
int fdb_io_read(uint32_t off, void *buf, uint32_t len)
{
    fdb_ctx_t *ctx = fdb_ctx();
    if (!ctx->ops || !ctx->ops->read)            {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_IO; }
    if (off + len < off)                         {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_INVALID_PARAM; }
    if (off + len > ctx->ops->size())            {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_INVALID_PARAM; }
    return ctx->ops->read(off, buf, len) == 0 ? FDB_OK : FDB_ERR_IO;
}

int fdb_io_write(uint32_t off, const void *buf, uint32_t len)
{
    fdb_ctx_t *ctx = fdb_ctx();
    if (!ctx->ops || !ctx->ops->write)           {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_IO; }
    if (off + len < off)                         {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_INVALID_PARAM; }
    if (off + len > ctx->ops->size())            {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_INVALID_PARAM; }
    int rc = ctx->ops->write(off, buf, len);
    FDB_LOG("%s %d %d", __FUNCTION__, __LINE__, rc);
    return rc == 0 ? FDB_OK : FDB_ERR_IO;
}

int fdb_io_erase(uint32_t off, uint32_t len)
{
    fdb_ctx_t *ctx = fdb_ctx();
    if (!ctx->ops)                               {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_IO; }
    if (!ctx->ops->erase)                        {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_OK; }   /* optional */
    if (off + len < off)                         {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_INVALID_PARAM; }
    if (off + len > ctx->ops->size())            {FDB_LOG("%s %d", __FUNCTION__, __LINE__); return FDB_ERR_INVALID_PARAM; }
    int rc = ctx->ops->erase(off, len);
    FDB_LOG("%s %d %d", __FUNCTION__, __LINE__, rc);
    return  rc == 0 ? FDB_OK : FDB_ERR_IO;
}
