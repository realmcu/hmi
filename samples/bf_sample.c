/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Big File (BF) extension samples.
 *
 * Demonstrates the typical use cases of the BF extension:
 *   1. Write a file (create -> append -> commit)
 *   2. Write a file with end-to-end CRC verification
 *   3. Read a file (open -> read -> close)
 *   4. Enumerate all files at boot with XIP address
 *   5. Query file info (stat / exists)
 *   6. Get XIP (memory-mapped) address for DMA / zero-copy access
 *   7. Delete by key
 *   8. Delete by absolute flash address
 *
 * Hardware assumption:
 *   - One 8 MB NOR flash chip ("norflash0", blk_size = 4096, write_gran = 1)
 *   - Two FAL partitions on the same chip:
 *       "fdb_kvdb1"  offset 0,       size 256 KB  (shared KVDB)
 *       "bf_data"    offset 256 KB,  size 7936 KB (big-file data region)
 */

#include <string.h>
#include <flashdb.h>

#ifdef FDB_USING_BF

#define FDB_LOG_TAG "[sample][bf]"

/* ── helpers ─────────────────────────────────────────────────────────────── */

static bool foreach_print_cb(const char *key, const struct fdb_bf_dirent *ent,
                              uint32_t xip_addr, void *arg)
{
    (void)arg;
    FDB_INFO("  %-20s  size=%-8u  cap=%-8u  xip=0x%08X  flags=0x%08X\n",
             key, (unsigned)ent->size, (unsigned)ent->capacity,
             (unsigned)xip_addr, (unsigned)ent->flags);
    return false;   /* return true to stop early */
}

/* ── sample entry points ─────────────────────────────────────────────────── */

/**
 * bf_sample_write  --  create a file and write data in chunks
 *
 * @param db    initialised BF object
 * @param key   user key (e.g. "firmware/app")
 * @param data  data buffer to write
 * @param len   data length in bytes
 */
void bf_sample_write(fdb_bf_t db, const char *key, const void *data, size_t len)
{
    fdb_err_t result;
    fdb_bf_file_t file;

    FDB_INFO("==================== bf_sample_write ====================\n");

    /* Step 1: create (reserve space).  max_size == len means an exact fit.
     * If the final size might grow, pass the upper bound as max_size. */
    result = fdb_bf_create(db, key, len, &file);
    if (result != FDB_NO_ERR) {
        FDB_INFO("create '%s' failed (%d)\n", key, (int)result);
        return;
    }
    FDB_INFO("created '%s', reserved %u bytes\n", key, (unsigned)file->capacity);

    /* Step 2: append data (may be called multiple times for chunked upload) */
    result = fdb_bf_append(file, data, len);
    if (result != FDB_NO_ERR) {
        FDB_INFO("append failed (%d), aborting\n", (int)result);
        fdb_bf_abort(file);
        return;
    }

    /* Step 3: commit without CRC -- pass NULL as the second argument.
     * The KVDB entry is written atomically here. */
    result = fdb_bf_commit(file, NULL);
    if (result != FDB_NO_ERR) {
        FDB_INFO("commit '%s' failed (%d)\n", key, (int)result);
        return;
    }
    FDB_INFO("'%s' committed, size=%u bytes\n", key, (unsigned)len);
    FDB_INFO("=========================================================\n");
}

/**
 * bf_sample_write_crc  --  write a file and record a caller-computed CRC32
 *
 * The caller accumulates the CRC across fdb_bf_append calls using any CRC32
 * function they prefer (here: FlashDB's fdb_calc_crc32).  The final value is
 * passed to fdb_bf_commit as a pointer; the module stores it in the directory
 * entry and sets FDB_BF_FLAG_CRC_VALID.
 */
void bf_sample_write_crc(fdb_bf_t db, const char *key, const void *data, size_t len)
{
    fdb_err_t result;
    fdb_bf_file_t file;
    uint32_t crc = 0;           /* initial value for fdb_calc_crc32 chain */

    FDB_INFO("==================== bf_sample_write_crc ====================\n");

    result = fdb_bf_create(db, key, len, &file);
    if (result != FDB_NO_ERR) {
        FDB_INFO("create '%s' failed (%d)\n", key, (int)result);
        return;
    }

    /* In a real chunked scenario this loop would call append + accumulate CRC
     * once per received chunk.  Here the whole buffer is one "chunk". */
    result = fdb_bf_append(file, data, len);
    if (result != FDB_NO_ERR) {
        fdb_bf_abort(file);
        return;
    }
    crc = fdb_calc_crc32(crc, data, len);   /* caller accumulates CRC */

    /* commit: pass &crc so the module stores it and sets FDB_BF_FLAG_CRC_VALID */
    result = fdb_bf_commit(file, &crc);
    if (result != FDB_NO_ERR) {
        FDB_INFO("commit '%s' failed (%d)\n", key, (int)result);
        return;
    }

    /* verify: read the directory entry back and inspect data_crc */
    {
        struct fdb_bf_dirent ent;
        if (fdb_bf_stat(db, key, &ent) == FDB_NO_ERR) {
            FDB_INFO("'%s' committed with CRC=0x%08X, flags=0x%08X\n",
                     key, (unsigned)ent.data_crc, (unsigned)ent.flags);
        }
    }
    FDB_INFO("=============================================================\n");
}

/**
 * bf_sample_boot_enum  --  enumerate all big files at boot
 *
 * Prints key, size, capacity, and XIP (memory-mapped) address for every file
 * stored in the BF extension.  The XIP address can be used directly for DMA
 * transfers or zero-copy reads without calling fdb_bf_read().
 */
void bf_sample_boot_enum(fdb_bf_t db)
{
    FDB_INFO("==================== bf_sample_boot_enum ====================\n");
    FDB_INFO("%-20s  %-8s  %-8s  %-10s  %s\n",
             "key", "size", "capacity", "xip_addr", "flags");
    fdb_bf_foreach(db, foreach_print_cb, NULL);
    FDB_INFO("=============================================================\n");
}

/**
 * bf_sample_stat  --  query a single file's metadata without opening it
 */
void bf_sample_stat(fdb_bf_t db, const char *key)
{
    struct fdb_bf_dirent ent;
    fdb_err_t result;

    FDB_INFO("==================== bf_sample_stat ====================\n");

    if (!fdb_bf_exists(db, key)) {
        FDB_INFO("'%s' does not exist\n", key);
        return;
    }

    result = fdb_bf_stat(db, key, &ent);
    if (result == FDB_NO_ERR) {
        FDB_INFO("key='%s'  offset=0x%X  capacity=%u  size=%u  flags=0x%X  crc=0x%X\n",
                 key, (unsigned)ent.offset, (unsigned)ent.capacity,
                 (unsigned)ent.size, (unsigned)ent.flags, (unsigned)ent.data_crc);
    }
    FDB_INFO("=========================================================\n");
}

/**
 * bf_sample_get_xip  --  get the absolute (XIP) flash address of a file
 *
 * On NOR flash with XIP (eXecute In Place), the returned address can be passed
 * directly to memcpy / DMA without any intermediate buffer.
 */
void bf_sample_get_xip(fdb_bf_t db, const char *key)
{
    uint32_t addr;
    size_t   size;
    fdb_err_t result;

    FDB_INFO("==================== bf_sample_get_xip ====================\n");

    result = fdb_bf_get_addr(db, key, &addr, &size);
    if (result == FDB_NO_ERR) {
        FDB_INFO("'%s'  xip_addr=0x%08X  size=%u bytes\n",
                 key, (unsigned)addr, (unsigned)size);
        /* example: pass addr directly to a DMA controller */
        /* dma_memcpy_from_flash(dest_buf, addr, size); */
    } else {
        FDB_INFO("get_addr '%s' failed (%d)\n", key, (int)result);
    }
    FDB_INFO("===========================================================\n");
}

/**
 * bf_sample_delete  --  delete a file by key
 */
void bf_sample_delete(fdb_bf_t db, const char *key)
{
    fdb_err_t result;

    FDB_INFO("==================== bf_sample_delete ====================\n");

    result = fdb_bf_delete(db, key);
    if (result == FDB_NO_ERR) {
        FDB_INFO("deleted '%s'\n", key);
    } else {
        FDB_INFO("delete '%s' failed (%d)\n", key, (int)result);
    }
    FDB_INFO("==========================================================\n");
}

/**
 * bf_sample_delete_by_addr  --  delete a file given its absolute flash address
 *
 * Useful when the caller has stored the XIP address but not the string key.
 * Any address within the file's reserved capacity range is accepted.
 */
void bf_sample_delete_by_addr(fdb_bf_t db, uint32_t addr)
{
    fdb_err_t result;

    FDB_INFO("==================== bf_sample_delete_by_addr ====================\n");

    result = fdb_bf_delete_by_addr(db, addr);
    if (result == FDB_NO_ERR) {
        FDB_INFO("deleted file at addr=0x%08X\n", (unsigned)addr);
    } else {
        FDB_INFO("delete_by_addr 0x%08X failed (%d)\n", (unsigned)addr, (int)result);
    }
    FDB_INFO("===================================================================\n");
}

/**
 * bf_sample  --  run all samples in sequence
 *
 * Typical call from main():
 *
 *   fal_init();
 *   fdb_kvdb_init(&kvdb, "env", "fdb_kvdb1", NULL, NULL);
 *   fdb_bf_init(&bf, &kvdb, "bf_data", NULL);
 *   bf_sample(&bf);
 */
void bf_sample(fdb_bf_t db)
{
    /* test payload: a short string simulating a firmware chunk */
    static const char payload[] =
        "Hello from Big File extension! "
        "This data lives in the NOR flash data partition.";
    static const char key1[] = "demo/hello";
    static const char key2[] = "demo/hello_crc";
    uint32_t xip_addr = 0;
    size_t   xip_size = 0;

    FDB_INFO("\n\n============ Big File extension sample start ============\n");

    /* 1. write without CRC */
    bf_sample_write(db, key1, payload, sizeof(payload) - 1);

    /* 2. write with CRC */
    bf_sample_write_crc(db, key2, payload, sizeof(payload) - 1);

    /* 3. list all files (boot enumeration) */
    bf_sample_boot_enum(db);

    /* 4. stat */
    bf_sample_stat(db, key1);

    /* 5. get XIP address, then delete by that address */
    if (fdb_bf_get_addr(db, key2, &xip_addr, &xip_size) == FDB_NO_ERR) {
        FDB_INFO("'%s' xip_addr=0x%08X, will delete by addr\n",
                 key2, (unsigned)xip_addr);
        bf_sample_delete_by_addr(db, xip_addr);
    }

    /* 6. delete key1 */
    bf_sample_delete(db, key1);

    /* 8. enumerate again -- should be empty now */
    FDB_INFO("After deleting all files:\n");
    bf_sample_boot_enum(db);

    FDB_INFO("============ Big File extension sample end ==============\n\n");
}

#endif /* FDB_USING_BF */
