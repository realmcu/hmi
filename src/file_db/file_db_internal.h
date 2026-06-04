/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * Internal types and helpers shared across the file_db source files.
 * Not part of the public API.
 */
#ifndef _FILE_DB_INTERNAL_H_
#define _FILE_DB_INTERNAL_H_

#include <stdint.h>
#include <stdbool.h>
#include "file_db.h"
#include "file_db_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FDB_SUPER_MAGIC     0x46444231u  /* 'F','D','B','1' */
#define FDB_SUPER_VERSION   1u

/* Directory entry states. The values are chosen so that on flash media a
 * fresh-erased word (0xFFFFFFFF) reads as FREE, and the WRITING -> VALID
 * transition only flips 1-bits to 0-bits in the state byte (0xFE -> 0xAA).
 * On a RAM port it does not matter.
 */
typedef enum
{
    FDB_ENTRY_FREE     = 0xFF,
    FDB_ENTRY_WRITING  = 0xFE,
    FDB_ENTRY_VALID    = 0xAA,
    FDB_ENTRY_DELETED  = 0x55,
} fdb_entry_state_t;

/*============================================================================*
 *                              On-storage layout
 *============================================================================*/

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t entry_size;
    uint32_t max_entries;
    uint32_t dir_offset;     /* offset of the directory table */
    uint32_t data_offset;    /* offset of the first data byte */
    uint32_t total_size;     /* whole region size (matches port->size()) */
    uint32_t crc;            /* CRC32 over the previous fields */
} fdb_super_t;

typedef struct
{
    uint8_t  state;          /* fdb_entry_state_t */
    uint8_t  flags;          /* FDB_FLAG_* */
    uint16_t reserved;
    uint32_t file_id;
    uint32_t offset;         /* data offset, absolute within the region */
    uint32_t capacity;       /* reserved bytes (>= size) */
    uint32_t expected_size;  /* declared at create time, 0 if unknown */
    uint32_t size;           /* committed bytes; only meaningful when VALID */
    uint32_t data_crc;       /* valid only when flags & HAS_DATA_CRC */
    uint32_t entry_crc;      /* CRC32 over the previous fields */
} fdb_dir_entry_t;

/*============================================================================*
 *                              In-RAM module state
 *============================================================================*/

struct fdb_file
{
    bool      in_use;
    bool      writing;       /* true between create and commit/abort */
    uint16_t  slot;          /* index in the directory table */
    uint32_t  file_id;
    uint32_t  offset;        /* data offset in the region */
    uint32_t  capacity;
    uint32_t  size;          /* uncommitted size while writing; committed size while reading */
    uint32_t  expected_size;
    uint8_t   flags;
};

typedef struct
{
    bool                   mounted;
    const fdb_port_ops_t  *ops;
    fdb_super_t            super;
    fdb_dir_entry_t        dir[FDB_MAX_ENTRIES];
    struct fdb_file        handles[FDB_MAX_OPEN_HANDLES];
} fdb_ctx_t;

/*============================================================================*
 *                              Internal helpers (implemented elsewhere)
 *============================================================================*/

fdb_ctx_t *fdb_ctx(void);

/* CRC32 (poly 0xEDB88320, init 0xFFFFFFFF, output xor 0xFFFFFFFF). */
uint32_t   fdb_crc32(uint32_t seed, const void *data, uint32_t len);
uint32_t   fdb_crc32_init(void);
uint32_t   fdb_crc32_finish(uint32_t state);

/* Directory I/O. */
uint32_t   fdb_dir_entry_off(uint16_t slot);
uint32_t   fdb_dir_entry_calc_crc(const fdb_dir_entry_t *e);
uint32_t   fdb_super_calc_crc(const fdb_super_t *s);
int        fdb_dir_write_entry(uint16_t slot, const fdb_dir_entry_t *e);
int        fdb_dir_read_entry(uint16_t slot, fdb_dir_entry_t *out);
int        fdb_super_write(const fdb_super_t *s);
int        fdb_super_read(fdb_super_t *out);

/* Allocator. */
int        fdb_alloc(uint32_t need_aligned, uint32_t *out_offset);
int        fdb_alloc_tail(uint32_t *out_offset, uint32_t *out_max_capacity);
uint32_t   fdb_align_up(uint32_t v, uint32_t a);

/* Find slot by id (any non-FREE state matched -> *out_slot). */
int        fdb_find_slot_by_id(uint32_t file_id, uint8_t state_mask, uint16_t *out_slot);
int        fdb_find_free_slot(uint16_t *out_slot);

/* state_mask helpers. */
#define FDB_STATE_BIT(s)    (1u << ((s) & 0xFFu))   /* not exact; we use direct compare */

/* Port I/O wrappers (range-checked, defensive). */
int        fdb_io_read(uint32_t off, void *buf, uint32_t len);
int        fdb_io_write(uint32_t off, const void *buf, uint32_t len);
int        fdb_io_erase(uint32_t off, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* _FILE_DB_INTERNAL_H_ */
