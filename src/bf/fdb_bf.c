/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Big File (BF) extension.
 *
 * Bulk data is stored in a dedicated FAL data partition managed by a rotating
 * allocator, while each file keeps a 24-byte directory entry inside a user-shared
 * KVDB (the entry KV uses the reserved prefix FDB_BF_KEY_PREFIX). A file commit is
 * a single atomic KV set; the allocator's occupancy is rebuilt by scanning the
 * "bf/" KVs, so no extra metadata is persisted in the data partition.
 *
 * NOTE: this is a brand-new extension that only references (never modifies) the
 * standalone file_db module at the repository root.
 */

#include <string.h>
#include <flashdb.h>
#include <fdb_low_lvl.h>
#include <fdb_bf.h>

#define FDB_LOG_TAG "[bf]"

#ifdef FDB_USING_BF

#define BF_PREFIX        FDB_BF_KEY_PREFIX

/* ==================== small helpers ==================== */

static uint32_t align_up(uint32_t v, uint32_t a)
{
    if (a == 0)
    {
        return v;
    }
    return ((v + a - 1) / a) * a;
}

/* build the full KV name "bf/<key>" into out, validating the key length */
static fdb_err_t make_full_key(const char *key, char *out, size_t out_size)
{
    size_t plen = strlen(BF_PREFIX);
    size_t klen;

    if (key == NULL)
    {
        return FDB_INVALID_PARAM;
    }
    klen = strlen(key);
    if (klen == 0 || klen >= FDB_BF_KEY_MAX)
    {
        return FDB_INVALID_PARAM;
    }
    if (plen + klen + 1 > out_size)
    {
        return FDB_INVALID_PARAM;
    }
    if (plen + klen >= FDB_KV_NAME_MAX)
    {
        /* the KVDB KV name has a hard limit */
        return FDB_INVALID_PARAM;
    }
    memcpy(out, BF_PREFIX, plen);
    memcpy(out + plen, key, klen);
    out[plen + klen] = '\0';

    return FDB_NO_ERR;
}

/* ==================== data partition I/O ==================== */

static fdb_err_t data_write(fdb_bf_t db, uint32_t off, const void *buf, size_t len)
{
    if ((uint64_t)off + len > db->data_size)
    {
        return FDB_WRITE_ERR;
    }
    return fal_partition_write(db->data_part, off, (const uint8_t *)buf,
                               len) >= 0 ? FDB_NO_ERR : FDB_WRITE_ERR;
}

static fdb_err_t data_erase(fdb_bf_t db, uint32_t off, size_t len)
{
    if ((uint64_t)off + len > db->data_size)
    {
        return FDB_ERASE_ERR;
    }
    return fal_partition_erase(db->data_part, off, len) >= 0 ? FDB_NO_ERR : FDB_ERASE_ERR;
}

/* ==================== directory entry (KVDB) access ==================== */

static bool read_dirent(fdb_bf_t db, const char *full_key, struct fdb_bf_dirent *ent)
{
    struct fdb_blob blob;

    fdb_kv_get_blob(db->dir_kvdb, full_key, fdb_blob_make(&blob, ent, sizeof(*ent)));

    return blob.saved.len == sizeof(*ent);
}

static fdb_err_t write_dirent(fdb_bf_t db, const char *full_key, const struct fdb_bf_dirent *ent)
{
    struct fdb_blob blob;

    /* bypass the reserved-prefix check: this is the only writer of "bf/" KVs */
    return _fdb_kv_set_blob_ex(db->dir_kvdb, full_key, fdb_blob_make(&blob, ent, sizeof(*ent)), true);
}

static fdb_err_t del_dirent(fdb_bf_t db, const char *full_key)
{
    return _fdb_kv_del_ex(db->dir_kvdb, full_key, true);
}

/* ==================== "bf/" KV enumeration ==================== */

typedef bool (*bf_raw_cb)(fdb_bf_t db, const char *user_key, const struct fdb_bf_dirent *ent,
                          void *arg);

/* iterate every valid directory entry; the callback returns true to stop early */
static void bf_iterate(fdb_bf_t db, bf_raw_cb cb, void *arg)
{
    struct fdb_kv_iterator it;
    struct fdb_blob blob;
    struct fdb_bf_dirent ent;
    char user_key[FDB_BF_KEY_MAX];
    size_t plen = strlen(BF_PREFIX);
    fdb_kv_t kv;
    size_t ulen;

    fdb_kv_iterator_init(db->dir_kvdb, &it);
    while (fdb_kv_iterate(db->dir_kvdb, &it))
    {
        kv = &it.curr_kv;
        if (kv->name_len <= plen)
        {
            continue;
        }
        if (strncmp(kv->name, BF_PREFIX, plen) != 0)
        {
            continue;
        }
        if (kv->value_len != sizeof(struct fdb_bf_dirent))
        {
            continue;
        }
        ulen = kv->name_len - plen;
        if (ulen >= sizeof(user_key))
        {
            continue;       /* defensive: should not happen */
        }
        memcpy(user_key, kv->name + plen, ulen);
        user_key[ulen] = '\0';

        if (fdb_blob_read((fdb_db_t)db->dir_kvdb,
                          fdb_kv_to_blob(kv, fdb_blob_make(&blob, &ent, sizeof(ent)))) != sizeof(ent))
        {
            continue;
        }
        if (cb(db, user_key, &ent, arg))
        {
            break;
        }
    }
}

/* ==================== open handle management ==================== */

static fdb_bf_file_t alloc_handle(fdb_bf_t db)
{
    int i;

    for (i = 0; i < FDB_BF_MAX_OPEN_HANDLES; i++)
    {
        if (!db->handles[i].in_use)
        {
            memset(&db->handles[i], 0, sizeof(db->handles[i]));
            db->handles[i].in_use = true;
            db->handles[i].db = db;
            return &db->handles[i];
        }
    }
    return NULL;
}

static void free_handle(fdb_bf_file_t file)
{
    file->in_use = false;
}

/* ==================== rotating allocator ==================== */

struct bf_region
{
    uint32_t start;
    uint32_t end;
};

struct collect_arg
{
    struct bf_region *arr;
    uint32_t cap;
    uint32_t n;
    bool overflow;
};

/* collect occupied [offset, offset+capacity) ranges, kept sorted by start */
static bool collect_cb(fdb_bf_t db, const char *key, const struct fdb_bf_dirent *ent, void *arg)
{
    struct collect_arg *c = (struct collect_arg *)arg;
    uint32_t i;

    (void)db;
    (void)key;

    if (ent->capacity == 0)
    {
        return false;
    }
    if (c->n >= c->cap)
    {
        c->overflow = true;
        return true;        /* stop iteration */
    }
    /* insertion sort by start offset */
    i = c->n;
    while (i > 0 && c->arr[i - 1].start > ent->offset)
    {
        c->arr[i] = c->arr[i - 1];
        i--;
    }
    c->arr[i].start = ent->offset;
    c->arr[i].end = ent->offset + ent->capacity;
    c->n++;

    return false;
}

/* find a free run of >= need bytes in [lo, hi); regions[] sorted ascending, non-overlapping */
static bool scan_range(const struct bf_region *r, uint32_t n, uint32_t lo, uint32_t hi,
                       uint32_t need, uint32_t *out)
{
    uint32_t p = lo;
    uint32_t i;

    for (i = 0; i < n && p < hi; i++)
    {
        if (r[i].end <= p)
        {
            continue;               /* region entirely before p */
        }
        if (r[i].start >= hi)
        {
            break;                  /* region beyond the search window */
        }
        if (r[i].start > p)
        {
            uint32_t gap_end = (r[i].start < hi) ? r[i].start : hi;
            if (gap_end - p >= need)
            {
                *out = p;
                return true;
            }
        }
        if (r[i].end > p)
        {
            p = r[i].end;           /* skip past this occupied region */
        }
    }
    if (p < hi && hi - p >= need)
    {
        *out = p;
        return true;
    }
    return false;
}

/* allocate a contiguous run of need (block-aligned) bytes, advancing the cursor */
static fdb_err_t bf_alloc(fdb_bf_t db, uint32_t need, uint32_t *out_off)
{
    struct bf_region regions[FDB_BF_MAX_ENTRIES];
    struct collect_arg ca;

    if (need == 0 || need > db->data_size)
    {
        return FDB_NO_SPACE;
    }

    ca.arr = regions;
    ca.cap = FDB_BF_MAX_ENTRIES;
    ca.n = 0;
    ca.overflow = false;
    bf_iterate(db, collect_cb, &ca);
    if (ca.overflow)
    {
        FDB_INFO("Error: too many big files (> %d) to track free space.\n", FDB_BF_MAX_ENTRIES);
        return FDB_NO_SPACE;
    }

    /* next-fit: from the write cursor to the end, then wrap and scan the whole region */
    if (db->write_cursor < db->data_size &&
        scan_range(regions, ca.n, db->write_cursor, db->data_size, need, out_off))
    {
        goto found;
    }
    if (scan_range(regions, ca.n, 0, db->data_size, need, out_off))
    {
        goto found;
    }
    return FDB_NO_SPACE;

found:
    db->write_cursor = *out_off + need;
    if (db->write_cursor >= db->data_size)
    {
        db->write_cursor = 0;
    }
    return FDB_NO_ERR;
}

/* ==================== lifecycle ==================== */

static bool cursor_cb(fdb_bf_t db, const char *key, const struct fdb_bf_dirent *ent, void *arg)
{
    uint32_t *max_end = (uint32_t *)arg;
    uint32_t end;

    (void)db;
    (void)key;

    end = ent->offset + ent->capacity;
    if (end > *max_end)
    {
        *max_end = end;
    }
    return false;
}

fdb_err_t fdb_bf_init(fdb_bf_t db, fdb_kvdb_t dir_kvdb, const char *data_part_name, void *user_data)
{
    const struct fal_partition *part;
    const struct fal_flash_dev *flash;
    uint32_t max_end = 0;

    if (db == NULL || dir_kvdb == NULL || data_part_name == NULL)
    {
        return FDB_INIT_FAILED;
    }

    memset(db, 0, sizeof(struct fdb_bf));
    db->dir_kvdb = dir_kvdb;
    db->user_data = user_data;

    /* register the reserved prefix so the user can not create/delete "bf/" KVs directly */
    fdb_kvdb_control(dir_kvdb, FDB_KVDB_CTRL_SET_RESERVED_PREFIX, (void *)FDB_BF_KEY_PREFIX);

    part = fal_partition_find(data_part_name);
    if (part == NULL)
    {
        FDB_INFO("Error: the data partition (%s) is not found.\n", data_part_name);
        return FDB_PART_NOT_FOUND;
    }
    flash = fal_flash_device_find(part->flash_name);
    if (flash == NULL)
    {
        FDB_INFO("Error: the flash device (%s) is not found.\n", part->flash_name);
        return FDB_PART_NOT_FOUND;
    }
    /* the Big File extension only supports NOR flash (write granularity == 1 bit) */
    if (flash->write_gran != 1)
    {
        FDB_INFO("Error: the Big File extension only supports NOR flash (write_gran must be 1, got %d).\n",
                 (int)flash->write_gran);
        return FDB_UNSUPPORTED;
    }

    db->data_part = part;
    db->data_flash = flash;
    db->blk_size = flash->blk_size;
    db->data_size = part->len;
    db->write_cursor = 0;
    db->inited = true;

    /* rebuild the write cursor from existing directory entries */
    bf_iterate(db, cursor_cb, &max_end);
    if (max_end >= db->data_size)
    {
        max_end = 0;
    }
    db->write_cursor = max_end;

    FDB_INFO("Big File extension initialized on partition '%s' (data %u bytes, block %u bytes).\n",
             data_part_name, (unsigned)db->data_size, (unsigned)db->blk_size);

    return FDB_NO_ERR;
}

fdb_err_t fdb_bf_deinit(fdb_bf_t db)
{
    if (db == NULL)
    {
        return FDB_INVALID_PARAM;
    }
    db->inited = false;

    return FDB_NO_ERR;
}

/* ==================== write path ==================== */

fdb_err_t fdb_bf_create(fdb_bf_t db, const char *key, size_t max_size, fdb_bf_file_t *out)
{
    fdb_err_t result;
    char full_key[FDB_KV_NAME_MAX];
    fdb_bf_file_t file;
    uint32_t need;
    uint32_t offset = 0;

    if (db == NULL || !db->inited || out == NULL)
    {
        return FDB_INIT_FAILED;
    }
    result = make_full_key(key, full_key, sizeof(full_key));
    if (result != FDB_NO_ERR)
    {
        return result;
    }

    need = align_up((uint32_t)max_size, db->blk_size);
    if (need == 0)
    {
        need = db->blk_size;        /* reserve at least one block */
    }

    file = alloc_handle(db);
    if (file == NULL)
    {
        return FDB_BUSY;
    }

    result = bf_alloc(db, need, &offset);
    if (result != FDB_NO_ERR)
    {
        free_handle(file);
        return result;
    }

    /* erase the reserved range before writing */
    result = data_erase(db, offset, need);
    if (result != FDB_NO_ERR)
    {
        free_handle(file);
        return result;
    }

    strncpy(file->key, key, sizeof(file->key) - 1);
    file->key[sizeof(file->key) - 1] = '\0';
    file->offset = offset;
    file->capacity = need;
    file->size = 0;

    *out = file;
    return FDB_NO_ERR;
}

fdb_err_t fdb_bf_append(fdb_bf_file_t file, const void *buf, size_t len)
{
    fdb_err_t result;

    if (file == NULL || !file->in_use)
    {
        return FDB_INVALID_PARAM;
    }
    if (len == 0)
    {
        return FDB_NO_ERR;
    }
    if (buf == NULL)
    {
        return FDB_INVALID_PARAM;
    }
    if ((uint64_t)file->size + len > file->capacity)
    {
        return FDB_NO_SPACE;
    }

    result = data_write(file->db, file->offset + file->size, buf, len);
    if (result != FDB_NO_ERR)
    {
        return result;
    }
    file->size += (uint32_t)len;

    return FDB_NO_ERR;
}

/*
 * fdb_bf_commit - atomic commit point.
 *
 * data_crc: pointer to a caller-computed CRC32 value, or NULL.
 *   NULL  -> commit without data integrity field (FDB_BF_FLAG_CRC_VALID not set).
 *   !NULL -> store *data_crc in the directory entry and set FDB_BF_FLAG_CRC_VALID.
 *
 * The caller is free to use any CRC32 implementation and accumulate it across
 * fdb_bf_append calls as they see fit.
 */
fdb_err_t fdb_bf_commit(fdb_bf_file_t file, const uint32_t *data_crc)
{
    fdb_err_t result;
    char full_key[FDB_KV_NAME_MAX];
    struct fdb_bf_dirent ent;
    fdb_bf_t db;

    if (file == NULL || !file->in_use)
    {
        return FDB_INVALID_PARAM;
    }
    db = file->db;

    result = make_full_key(file->key, full_key, sizeof(full_key));
    if (result != FDB_NO_ERR)
    {
        return result;
    }

    memset(&ent, 0, sizeof(ent));
    ent.offset   = file->offset;
    ent.size     = file->size;
    ent.capacity = align_up(file->size, db->blk_size);
    ent.flags    = 0;
    ent.reserved = 0;
    if (data_crc != NULL)
    {
        ent.data_crc = *data_crc;
        ent.flags   |= FDB_BF_FLAG_CRC_VALID;
    }

    /* the atomic commit point: one KV set */
    result = write_dirent(db, full_key, &ent);

    free_handle(file);
    return result;
}

fdb_err_t fdb_bf_abort(fdb_bf_file_t file)
{
    if (file == NULL || !file->in_use)
    {
        return FDB_INVALID_PARAM;
    }
    /* no KVDB entry was written, so the reserved data range is simply released */
    free_handle(file);

    return FDB_NO_ERR;
}

/* ==================== management ==================== */

fdb_err_t fdb_bf_delete(fdb_bf_t db, const char *key)
{
    fdb_err_t result;
    char full_key[FDB_KV_NAME_MAX];
    struct fdb_bf_dirent ent;

    if (db == NULL || !db->inited)
    {
        return FDB_INIT_FAILED;
    }
    result = make_full_key(key, full_key, sizeof(full_key));
    if (result != FDB_NO_ERR)
    {
        return result;
    }
    if (!read_dirent(db, full_key, &ent))
    {
        return FDB_NOT_FOUND;
    }
    /* removing the directory entry makes the file invisible; the data range is
     * reclaimed lazily by the allocator (no immediate erase needed) */
    return del_dirent(db, full_key);
}

struct find_by_addr_arg
{
    uint32_t off;
    bool found;
    char key[FDB_BF_KEY_MAX];
};

static bool find_by_addr_cb(fdb_bf_t db, const char *key, const struct fdb_bf_dirent *ent,
                            void *arg)
{
    struct find_by_addr_arg *fa = (struct find_by_addr_arg *)arg;

    (void)db;

    if (ent->capacity == 0)
    {
        return false;
    }
    if (fa->off >= ent->offset && fa->off < ent->offset + ent->capacity)
    {
        strncpy(fa->key, key, sizeof(fa->key) - 1);
        fa->key[sizeof(fa->key) - 1] = '\0';
        fa->found = true;
        return true;        /* stop */
    }
    return false;
}

fdb_err_t fdb_bf_delete_by_addr(fdb_bf_t db, uint32_t addr)
{
    struct find_by_addr_arg fa;
    char full_key[FDB_KV_NAME_MAX];
    uint32_t base;
    fdb_err_t result;

    if (db == NULL || !db->inited)
    {
        return FDB_INIT_FAILED;
    }
    base = db->data_flash->addr + db->data_part->offset;
    if (addr < base)
    {
        return FDB_NOT_FOUND;
    }

    fa.off = addr - base;
    fa.found = false;
    fa.key[0] = '\0';
    /* iterate read-only to locate the key, then delete it after the iteration */
    bf_iterate(db, find_by_addr_cb, &fa);
    if (!fa.found)
    {
        return FDB_NOT_FOUND;
    }

    result = make_full_key(fa.key, full_key, sizeof(full_key));
    if (result != FDB_NO_ERR)
    {
        return result;
    }
    return del_dirent(db, full_key);
}

/* ==================== reset ==================== */

/* How many directory keys one reset pass carries. Bounds the stack cost to
 * BF_RESET_BATCH * FDB_KV_NAME_MAX bytes; the reset loops until a pass comes up
 * empty, so this caps memory, not the number of files that can be cleared. */
#define BF_RESET_BATCH   4

/*
 * Collect up to cap "bf/" KV names (full names, prefix included).
 *
 * Deliberately NOT bf_iterate(): that one also requires the value to be a
 * well-formed dirent, and a reset must clear entries whose value was left the
 * wrong length by an interrupted write -- exactly the state you reset from.
 * Matching on the reserved prefix alone is the whole point here.
 */
static uint32_t reset_collect(fdb_bf_t db, char keys[][FDB_KV_NAME_MAX], uint32_t cap)
{
    struct fdb_kv_iterator it;
    size_t plen = strlen(BF_PREFIX);
    uint32_t n = 0;
    fdb_kv_t kv;

    fdb_kv_iterator_init(db->dir_kvdb, &it);
    while (n < cap && fdb_kv_iterate(db->dir_kvdb, &it))
    {
        kv = &it.curr_kv;
        if (kv->name_len <= plen)
        {
            continue;
        }
        if (strncmp(kv->name, BF_PREFIX, plen) != 0)
        {
            continue;
        }
        if (kv->name_len >= FDB_KV_NAME_MAX)
        {
            continue;       /* defensive: cannot be NUL-terminated in the buffer */
        }
        /* kv->name is not guaranteed NUL-terminated, so copy by length */
        memcpy(keys[n], kv->name, kv->name_len);
        keys[n][kv->name_len] = '\0';
        n++;
    }

    return n;
}

fdb_err_t fdb_bf_reset(fdb_bf_t db, uint32_t *out_removed)
{
    char keys[BF_RESET_BATCH][FDB_KV_NAME_MAX];
    uint32_t removed = 0;
    uint32_t n;
    uint32_t i;
    fdb_err_t result;

    if (out_removed != NULL)
    {
        *out_removed = 0;
    }
    if (db == NULL || !db->inited)
    {
        return FDB_INIT_FAILED;
    }

    /* An open write session owns a reserved data range and would keep appending
     * into bytes this call is about to erase, then commit an entry describing
     * them. Refuse instead of stranding its handle. */
    for (i = 0; i < FDB_BF_MAX_OPEN_HANDLES; i++)
    {
        if (db->handles[i].in_use)
        {
            FDB_INFO("Error: cannot reset while a write session is open ('%s').\n",
                     db->handles[i].key);
            return FDB_BUSY;
        }
    }

    /* 1. Drop every directory entry, a batch per pass.
     *
     * Collect-then-delete (the same shape fdb_bf_delete_by_addr uses) rather
     * than deleting inside the walk: the KV iterator advances from the entry it
     * is standing on, so mutating that entry mid-walk is a hazard not worth
     * relying on. Deleting only flips a status field in place, so the next pass
     * simply no longer sees what this one removed. */
    for (;;)
    {
        n = reset_collect(db, keys, BF_RESET_BATCH);
        if (n == 0)
        {
            break;
        }
        for (i = 0; i < n; i++)
        {
            result = del_dirent(db, keys[i]);
            if (result != FDB_NO_ERR)
            {
                /* Bail out rather than spin: the next pass would hand back the
                 * very same key and loop forever. */
                FDB_INFO("Error: failed to delete the entry '%s' (%d).\n", keys[i], (int)result);
                if (out_removed != NULL)
                {
                    *out_removed = removed;
                }
                return result;
            }
            removed++;
        }
    }

    /* Every figure the API reports -- file_count, used_size, valid_size,
     * largest_free -- is recomputed from the directory on each query, so an
     * empty directory *is* the reset counter state. Only the cached allocation
     * cursor lives in RAM. */
    db->write_cursor = 0;

    if (out_removed != NULL)
    {
        *out_removed = removed;
    }

    /* 2. Erase the payload.
     *
     * Not needed to make the files disappear (step 1 did that, and create()
     * erases what it hands out), but asked for explicitly so a reset leaves no
     * readable remnants of the old pictures behind. This is the expensive part:
     * one sector erase per blk_size of the partition. The FAL port kicks the
     * watchdog inside its erase loop. */
    FDB_INFO("Resetting the Big File area: %u entries removed, erasing %u bytes.\n",
             (unsigned)removed, (unsigned)db->data_size);

    result = data_erase(db, 0, db->data_size);
    if (result != FDB_NO_ERR)
    {
        /* The directory is already empty, so the area reads as "no files"
         * either way; only the stale bytes survive. Report it so a caller can
         * retry the erase. */
        FDB_INFO("Error: the Big File data erase failed (%d).\n", (int)result);
        return FDB_ERASE_ERR;
    }

    return FDB_NO_ERR;
}

bool fdb_bf_exists(fdb_bf_t db, const char *key)
{
    char full_key[FDB_KV_NAME_MAX];
    struct fdb_bf_dirent ent;

    if (db == NULL || !db->inited)
    {
        return false;
    }
    if (make_full_key(key, full_key, sizeof(full_key)) != FDB_NO_ERR)
    {
        return false;
    }
    return read_dirent(db, full_key, &ent);
}

fdb_err_t fdb_bf_stat(fdb_bf_t db, const char *key, struct fdb_bf_dirent *out)
{
    char full_key[FDB_KV_NAME_MAX];
    fdb_err_t result;

    if (db == NULL || !db->inited || out == NULL)
    {
        return FDB_INVALID_PARAM;
    }
    result = make_full_key(key, full_key, sizeof(full_key));
    if (result != FDB_NO_ERR)
    {
        return result;
    }
    if (!read_dirent(db, full_key, out))
    {
        return FDB_NOT_FOUND;
    }
    return FDB_NO_ERR;
}

struct foreach_arg
{
    fdb_bf_iter_cb cb;
    void *arg;
};

static bool foreach_cb(fdb_bf_t db, const char *key, const struct fdb_bf_dirent *ent, void *arg)
{
    struct foreach_arg *fo = (struct foreach_arg *)arg;
    uint32_t xip = db->data_flash->addr + db->data_part->offset + ent->offset;

    return fo->cb(key, ent, xip, fo->arg);
}

fdb_err_t fdb_bf_foreach(fdb_bf_t db, fdb_bf_iter_cb cb, void *arg)
{
    struct foreach_arg fo;

    if (db == NULL || !db->inited || cb == NULL)
    {
        return FDB_INVALID_PARAM;
    }
    fo.cb = cb;
    fo.arg = arg;
    bf_iterate(db, foreach_cb, &fo);

    return FDB_NO_ERR;
}

fdb_err_t fdb_bf_get_addr(fdb_bf_t db, const char *key, uint32_t *out_addr, size_t *out_size)
{
    char full_key[FDB_KV_NAME_MAX];
    struct fdb_bf_dirent ent;
    fdb_err_t result;

    if (db == NULL || !db->inited)
    {
        return FDB_INIT_FAILED;
    }
    result = make_full_key(key, full_key, sizeof(full_key));
    if (result != FDB_NO_ERR)
    {
        return result;
    }
    if (!read_dirent(db, full_key, &ent))
    {
        return FDB_NOT_FOUND;
    }
    if (out_addr)
    {
        *out_addr = db->data_flash->addr + db->data_part->offset + ent.offset;
    }
    if (out_size)
    {
        *out_size = ent.size;
    }
    return FDB_NO_ERR;
}

/* ==================== space accounting ==================== */

struct space_arg
{
    struct bf_region *arr;
    uint32_t cap;
    uint32_t n;
    bool overflow;
    uint32_t used;
    uint32_t valid;
};

/* Same sorted insertion as collect_cb, but also totals the byte counts.  Kept
 * separate rather than extending collect_arg so the allocator's hot path stays
 * untouched. */
static bool space_cb(fdb_bf_t db, const char *key, const struct fdb_bf_dirent *ent, void *arg)
{
    struct space_arg *s = (struct space_arg *)arg;
    uint32_t i;

    (void)db;
    (void)key;

    if (ent->capacity == 0)
    {
        return false;
    }

    s->used += ent->capacity;
    s->valid += ent->size;

    if (s->n >= s->cap)
    {
        /* Keep counting bytes, but the region array -- and therefore
         * largest_free -- can no longer be trusted. */
        s->overflow = true;
        return false;
    }
    i = s->n;
    while (i > 0 && s->arr[i - 1].start > ent->offset)
    {
        s->arr[i] = s->arr[i - 1];
        i--;
    }
    s->arr[i].start = ent->offset;
    s->arr[i].end = ent->offset + ent->capacity;
    s->n++;

    return false;
}

/* Walk the gaps between occupied regions and return the widest one.  Mirrors
 * scan_range()'s traversal, but measures instead of stopping at the first fit --
 * this is the number that decides whether fdb_bf_create() will succeed. */
static uint32_t largest_gap(const struct bf_region *r, uint32_t n, uint32_t total)
{
    uint32_t best = 0;
    uint32_t p = 0;
    uint32_t i;

    for (i = 0; i < n && p < total; i++)
    {
        if (r[i].end <= p)
        {
            continue;
        }
        if (r[i].start > p)
        {
            uint32_t gap_end = (r[i].start < total) ? r[i].start : total;

            if (gap_end - p > best)
            {
                best = gap_end - p;
            }
        }
        if (r[i].end > p)
        {
            p = r[i].end;
        }
    }
    if (p < total && total - p > best)
    {
        best = total - p;
    }

    return best;
}

fdb_err_t fdb_bf_space(fdb_bf_t db, struct fdb_bf_space *out)
{
    struct bf_region regions[FDB_BF_MAX_ENTRIES];
    struct space_arg sa;

    if (db == NULL || !db->inited || out == NULL)
    {
        return FDB_INVALID_PARAM;
    }

    sa.arr = regions;
    sa.cap = FDB_BF_MAX_ENTRIES;
    sa.n = 0;
    sa.overflow = false;
    sa.used = 0;
    sa.valid = 0;
    bf_iterate(db, space_cb, &sa);

    memset(out, 0, sizeof(*out));
    out->total_size = db->data_size;
    out->blk_size = db->blk_size;
    out->file_count = sa.n;
    out->valid_size = sa.valid;
    out->truncated = sa.overflow;

    /* Defensive: a corrupt dirent could claim more capacity than the partition
     * holds.  Clamp rather than underflow free_size. */
    out->used_size = (sa.used > db->data_size) ? db->data_size : sa.used;
    out->free_size = db->data_size - out->used_size;

    if (sa.overflow)
    {
        /* Region array incomplete -- report the fragmentation-blind upper bound
         * instead of a wrong contiguous figure. */
        out->largest_free = out->free_size;
    }
    else
    {
        out->largest_free = largest_gap(regions, sa.n, db->data_size);
    }

    return FDB_NO_ERR;
}

uint32_t fdb_bf_free_size(fdb_bf_t db)
{
    struct fdb_bf_space sp;

    if (fdb_bf_space(db, &sp) != FDB_NO_ERR)
    {
        return 0;
    }
    return sp.largest_free;
}

#endif /* FDB_USING_BF */