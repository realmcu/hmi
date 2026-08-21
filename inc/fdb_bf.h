/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Big File (BF) extension public APIs.
 *
 * The Big File extension stores bulk data in a dedicated FAL data partition while
 * keeping a per-file directory entry inside a user-shared KVDB. Each directory
 * entry is a single KV whose name uses the reserved prefix FDB_BF_KEY_PREFIX, so a
 * file commit equals one atomic KV set. The data partition must live on a NOR flash
 * (write granularity == 1 bit) and is managed by a rotating allocator.
 */

#ifndef _FDB_BF_H_
#define _FDB_BF_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <fdb_cfg.h>
#include <fdb_def.h>

#ifdef FDB_USING_BF

#ifndef FDB_USING_FAL_MODE
#error "The Big File extension (FDB_USING_BF) requires FDB_USING_FAL_MODE."
#endif

#include <fal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the reserved KV name prefix used by the directory entries */
#ifndef FDB_BF_KEY_PREFIX
#define FDB_BF_KEY_PREFIX            "bf/"
#endif

/* the max length (including the '\0') of a user file key, excluding the prefix */
#ifndef FDB_BF_KEY_MAX
#define FDB_BF_KEY_MAX               32
#endif

/* the max number of files that can be opened/created at the same time */
#ifndef FDB_BF_MAX_OPEN_HANDLES
#define FDB_BF_MAX_OPEN_HANDLES      4
#endif

/* the max number of files the allocator can track when searching for free space */
#ifndef FDB_BF_MAX_ENTRIES
#define FDB_BF_MAX_ENTRIES           32
#endif

/* directory entry flag bits (32 bit) */
#define FDB_BF_FLAG_CRC_VALID        0x00000001U     /**< data_crc field holds a valid CRC32 of the data */

/**
 * Big File directory entry. Stored as the value of one KVDB KV (24 bytes).
 */
struct fdb_bf_dirent
{
    uint32_t offset;        /**< data offset relative to the data partition start */
    uint32_t capacity;      /**< allocated capacity, aligned to flash block size */
    uint32_t size;          /**< valid data length (<= capacity) */
    uint32_t data_crc;      /**< CRC32 of data range [offset, offset + size), valid when FDB_BF_FLAG_CRC_VALID set */
    uint32_t flags;         /**< flag bits, @see FDB_BF_FLAG_* */
    uint32_t reserved;      /**< reserved for future use, set to 0 */
};
typedef struct fdb_bf_dirent *fdb_bf_dirent_t;

/**
 * Big File open/write handle.
 */
struct fdb_bf_file
{
    struct fdb_bf *db;                 /**< owner BF object */
    bool      in_use;                  /**< handle slot is allocated */
    char      key[FDB_BF_KEY_MAX];     /**< user file key (without prefix) */
    uint32_t  offset;                  /**< data offset in the data partition */
    uint32_t  capacity;                /**< allocated capacity */
    uint32_t  size;                    /**< current valid data length */
};
typedef struct fdb_bf_file *fdb_bf_file_t;

/**
 * Big File database object.
 */
struct fdb_bf
{
    fdb_kvdb_t
    dir_kvdb;                          /**< user-shared KVDB holding the directory entries */
    const struct fal_partition *data_part;         /**< data partition */
    const struct fal_flash_dev *data_flash;        /**< flash device of the data partition */
    uint32_t
    blk_size;                          /**< erase block size of the data flash (allocation unit) */
    uint32_t    data_size;                         /**< data partition length in bytes */
    uint32_t    write_cursor;                      /**< rotating allocation cursor, rebuilt at init */
    struct fdb_bf_file handles[FDB_BF_MAX_OPEN_HANDLES];
    void       *user_data;
    bool        inited;                            /**< initialized successfully */
};
typedef struct fdb_bf *fdb_bf_t;

/**
 * The callback used by fdb_bf_foreach().
 *
 * @param key the user file key (without prefix)
 * @param ent the directory entry
 * @param xip_addr the absolute (memory-mapped/XIP) flash address of the file data
 * @param arg the user argument passed to fdb_bf_foreach()
 *
 * @return return true to stop the iteration, false to continue
 */
typedef bool (*fdb_bf_iter_cb)(const char *key, const struct fdb_bf_dirent *ent, uint32_t xip_addr,
                               void *arg);

/**
 * Big File space usage. Filled in by fdb_bf_space().
 *
 * The allocator only ever hands out a *contiguous* block-aligned run, so
 * `largest_free` -- not `free_size` -- decides whether an fdb_bf_create() of a
 * given size can succeed. The two differ once the partition is fragmented.
 */
struct fdb_bf_space
{
    uint32_t total_size;      /**< data partition length, == db->data_size */
    uint32_t used_size;       /**< sum of every entry's allocated capacity */
    uint32_t free_size;       /**< total_size - used_size, possibly fragmented */
    uint32_t largest_free;    /**< biggest contiguous free run; the usable limit */
    uint32_t valid_size;      /**< sum of every entry's valid data length */
    uint32_t file_count;      /**< number of committed big files */
    uint32_t blk_size;        /**< allocation unit (erase block size) */
    bool     truncated;       /**< true: > FDB_BF_MAX_ENTRIES files, figures are partial */
};

/* lifecycle */
fdb_err_t fdb_bf_init(fdb_bf_t db, fdb_kvdb_t dir_kvdb, const char *data_part_name,
                      void *user_data);
fdb_err_t fdb_bf_deinit(fdb_bf_t db);

/* write: create -> append... -> commit(data_crc) / abort
 *
 * fdb_bf_commit(file, NULL)       -- commit without data CRC
 * fdb_bf_commit(file, &crc_val)   -- commit and store caller-computed CRC32
 *                                    (sets FDB_BF_FLAG_CRC_VALID in flags)
 *
 * The caller is responsible for accumulating the CRC across fdb_bf_append
 * calls using any CRC32 implementation they prefer.
 */
fdb_err_t fdb_bf_create(fdb_bf_t db, const char *key, size_t max_size, fdb_bf_file_t *out);
fdb_err_t fdb_bf_append(fdb_bf_file_t file, const void *buf, size_t len);
fdb_err_t fdb_bf_commit(fdb_bf_file_t file, const uint32_t *data_crc);
fdb_err_t fdb_bf_abort(fdb_bf_file_t file);

/* manage */
fdb_err_t fdb_bf_delete(fdb_bf_t db, const char *key);
fdb_err_t fdb_bf_delete_by_addr(fdb_bf_t db, uint32_t addr);

/**
 * Factory-reset the Big File area: drop every directory entry, then erase the
 * whole data partition.
 *
 * Only KVs carrying the reserved FDB_BF_KEY_PREFIX are removed -- the directory
 * KVDB is shared with the application, and its ordinary KVs are left untouched.
 * (This is why fdb_kv_set_default() is NOT the right tool here: it would wipe
 * the application's own settings along with the file directory.)
 *
 * Every "record" a caller can observe -- file_count, used_size, valid_size, the
 * allocator's write cursor -- is *derived* from the directory entries on each
 * query, never stored, so all of them fall back to empty as a consequence of
 * clearing the directory. There is no separate counter to reset.
 *
 * Order is deliberate: the directory is cleared *before* the payload is erased.
 * A power loss during the (multi-second) erase then leaves entries gone and data
 * partly erased, which is consistent. The reverse order would leave entries
 * pointing at erased bytes, i.e. files that exist but read as 0xFF.
 *
 * @param db          the BF object
 * @param out_removed optional, receives the number of entries deleted; may be
 *                    NULL. It is filled in even when the erase step later fails.
 *
 * @return FDB_NO_ERR on success;
 *         FDB_BUSY if a write session (fdb_bf_create) is still open -- commit or
 *         abort it first, since resetting under it would strand its handle;
 *         FDB_ERASE_ERR if the payload erase failed (directory is already empty).
 */
fdb_err_t fdb_bf_reset(fdb_bf_t db, uint32_t *out_removed);
bool      fdb_bf_exists(fdb_bf_t db, const char *key);
fdb_err_t fdb_bf_stat(fdb_bf_t db, const char *key, struct fdb_bf_dirent *out);
fdb_err_t fdb_bf_foreach(fdb_bf_t db, fdb_bf_iter_cb cb, void *arg);
fdb_err_t fdb_bf_get_addr(fdb_bf_t db, const char *key, uint32_t *out_addr, size_t *out_size);

/* space accounting
 *
 * fdb_bf_space()      -- full report, one pass over the directory entries
 * fdb_bf_free_size()  -- shorthand for the number that actually gates a create:
 *                        the largest contiguous free run, 0 on error
 */
fdb_err_t fdb_bf_space(fdb_bf_t db, struct fdb_bf_space *out);
uint32_t  fdb_bf_free_size(fdb_bf_t db);

#ifdef __cplusplus
}
#endif

#endif /* FDB_USING_BF */

#endif /* _FDB_BF_H_ */
