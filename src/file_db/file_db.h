/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * file_db --- a tiny filesystem-like store for chunked file uploads on
 * embedded targets. See README at the head of file_db.c for the on-disk
 * layout, commit protocol and recovery semantics.
 */
#ifndef _FILE_DB_H_
#define _FILE_DB_H_

#include <stdint.h>
#include <stdbool.h>
#include "file_db_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 *                              Error codes
 *============================================================================*/

typedef enum
{
    FDB_OK                 = 0,
    FDB_ERR_INVALID_PARAM  = -1,
    FDB_ERR_NOT_MOUNTED    = -2,
    FDB_ERR_NOT_FOUND      = -3,
    FDB_ERR_EXISTS         = -4,
    FDB_ERR_NO_SPACE       = -5,
    FDB_ERR_DIR_FULL       = -6,
    FDB_ERR_IO             = -7,
    FDB_ERR_CORRUPT        = -8,
    FDB_ERR_STATE          = -9,
    FDB_ERR_BUSY           = -10,
    FDB_ERR_TOO_MANY_OPEN  = -11,
    FDB_ERR_UNSUPPORTED    = -12,
} fdb_err_t;

/*============================================================================*
 *                              Types
 *============================================================================*/

/* Per-file flags exposed to callers. */
#define FDB_FLAG_HAS_DATA_CRC   (1u << 0)   /* file has end-to-end data CRC32 */
#define FDB_FLAG_SIZE_KNOWN     (1u << 1)   /* exact size declared at create  */

typedef struct
{
    uint32_t file_id;
    uint32_t offset;        /* absolute offset within the storage region */
    uint32_t capacity;      /* reserved bytes */
    uint32_t size;          /* committed bytes (valid only when state==VALID) */
    uint32_t expected_size; /* declared at create time, 0 if unknown */
    uint32_t data_crc;      /* valid only when flags & HAS_DATA_CRC */
    uint8_t  flags;
    uint8_t  state;         /* see file_db_internal.h fdb_entry_state_t */
} fdb_file_info_t;

/* Opaque streaming handle. */
typedef struct fdb_file fdb_file_t;

/* Iterator callback. Return non-zero to stop iteration early. */
typedef int (*fdb_iter_cb_t)(const fdb_file_info_t *info, void *user);

/* Aggregate directory / storage statistics. */
typedef struct
{
    uint32_t max_entries;       /* directory table capacity                */
    uint32_t entries_used;      /* non-FREE slots (VALID + WRITING + DEL)  */
    uint32_t file_count;        /* VALID slots only                        */
    uint32_t data_region_size;  /* total_size - data_offset                */
    uint32_t bytes_used;        /* sum of size  for VALID files            */
    uint32_t bytes_reserved;    /* sum of capacity for VALID files         */
    uint32_t bytes_free;        /* free bytes in the data region           */
    uint32_t largest_free_hole; /* largest contiguous free run             */
} fdb_dir_info_t;

/*============================================================================*
 *                              Lifecycle
 *============================================================================*/

/**
 * Bind a port. Must be called before format/mount.
 */
int fdb_init(const fdb_port_ops_t *ops);

/**
 * Tear down. Closes any open handles.
 */
int fdb_deinit(void);

/**
 * Wipe the region and lay down a fresh superblock + empty directory.
 * After format() the module is mounted and ready to use.
 */
int fdb_format(void);

/**
 * Read the superblock and directory, reconcile WRITING / corrupted entries,
 * and become ready for I/O. Returns FDB_ERR_CORRUPT if the superblock is
 * unreadable; the caller may then fdb_format().
 */
int fdb_mount(void);

bool fdb_is_mounted(void);

/*============================================================================*
 *                              Streaming write
 *============================================================================*/

/**
 * Open a new file for writing.
 *
 * @param file_id        Unique caller-supplied id. Must not already exist.
 * @param max_size       Upper bound on bytes to be written. The allocator
 *                       reserves capacity = align_up(max_size). Pass 0 to
 *                       allocate at the tail (size determined at commit).
 * @param expected_size  Exact size if known (used by commit to verify
 *                       full reception when no data CRC is provided).
 *                       Pass 0 if unknown.
 * @param out            Receives the streaming handle on success.
 */
int fdb_create(uint32_t file_id, uint32_t max_size, uint32_t expected_size,
               fdb_file_t **out);

/**
 * Append a chunk of data. Bounded by capacity.
 */
int fdb_append(fdb_file_t *f, const void *data, uint32_t len);

/**
 * Commit the file (state -> VALID). Without a data CRC, integrity is
 * limited to "the caller decided we are done". If expected_size was
 * declared at create time, the actual written size must match.
 */
int fdb_commit(fdb_file_t *f);

/**
 * Commit with an end-to-end data CRC32 (caller-computed).
 */
int fdb_commit_crc(fdb_file_t *f, uint32_t data_crc);

/**
 * Drop an in-progress write. The slot is released and space reclaimed.
 */
int fdb_abort(fdb_file_t *f);

/*============================================================================*
 *                              Read / metadata
 *============================================================================*/

int fdb_open(uint32_t file_id, fdb_file_t **out);
int fdb_read(fdb_file_t *f, uint32_t offset, void *buf, uint32_t len);
int fdb_close(fdb_file_t *f);

int fdb_delete(uint32_t file_id);

/**
 * Delete the file whose data range covers the given absolute address.
 * Any `addr` such that
 *      entry.offset <= (addr - base) < entry.offset + entry.capacity
 * matches - i.e. the address can point anywhere inside the file's reserved
 * range, not just its first byte. Returns FDB_ERR_NOT_FOUND if no VALID
 * file covers the address, FDB_ERR_UNSUPPORTED if the port has no
 * `base_addr` hook.
 */
int fdb_delete_by_addr(uintptr_t addr);

int fdb_exists(uint32_t file_id);
int fdb_stat(uint32_t file_id, fdb_file_info_t *out);
int fdb_foreach(fdb_iter_cb_t cb, void *user);

/*--- Directory introspection ----------------------------------------------*/

/**
 * Number of valid (committed) files currently stored.
 */
int fdb_get_file_count(uint32_t *out);

/**
 * Same as fdb_get_file_count() but returns the count directly.
 * Returns 0 when not mounted (callers wanting to distinguish that case
 * should use fdb_get_file_count() instead).
 */
uint32_t fdb_file_count(void);

/**
 * Indexed iteration: fetch the absolute address, size and id of the
 * `index`-th valid file (0-based, in directory-slot order).
 *
 * The address is the CPU-addressable base + the file's in-region offset,
 * suitable for handing to memory-mapped readers, DMA, etc. Requires the
 * port to implement the optional `base_addr` hook; otherwise returns
 * FDB_ERR_UNSUPPORTED.
 *
 * `out_addr` is mandatory; `out_size` and `out_id` may be NULL.
 *
 * Typical loop:
 *
 *     uint32_t n = fdb_file_count();
 *     for (uint32_t i = 0; i < n; ++i) {
 *         uintptr_t addr; uint32_t size, id;
 *         fdb_get_file_addr(i, &addr, &size, &id);
 *         ...
 *     }
 */
int fdb_get_file_addr(uint32_t index, uintptr_t *out_addr,
                      uint32_t *out_size, uint32_t *out_id);

/**
 * Lookup the absolute address (and optionally size) of a file by id.
 * Mirrors fdb_get_file_addr() but keyed on file_id instead of index.
 * Requires the port to implement `base_addr`; otherwise returns
 * FDB_ERR_UNSUPPORTED. `out_size` may be NULL.
 */
int fdb_get_addr_by_id(uint32_t file_id, uintptr_t *out_addr,
                       uint32_t *out_size);

/**
 * Snapshot all valid files into the caller-provided array.
 *
 * @param out        Caller buffer of `max` entries.
 * @param max        Buffer capacity (in entries).
 * @param count_out  Receives the number of entries actually filled.
 *                   May be less than the total file count when the
 *                   buffer is too small; the rest are silently truncated.
 */
int fdb_get_file_list(fdb_file_info_t *out, uint32_t max, uint32_t *count_out);

/**
 * Aggregate directory & data-region statistics.
 */
int fdb_get_dir_info(fdb_dir_info_t *out);

/**
 * Verify a file's stored data CRC. Returns FDB_OK on match, FDB_ERR_CORRUPT
 * on mismatch, FDB_ERR_UNSUPPORTED if the file has no data CRC.
 */
int fdb_verify(uint32_t file_id);

/*============================================================================*
 *                              Debug / introspection
 *============================================================================*/

void fdb_dump_super(void);
void fdb_dump_dir(void);
void fdb_dump_usage(void);
void fdb_dump_file(uint32_t file_id, uint32_t head_n);

#ifdef __cplusplus
}
#endif

#endif /* _FILE_DB_H_ */
