/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB Big-File (BF) test — create + append + commit + XIP read.
 *
 * How BF differs most from KV/TS:
 *   1) A separate data partition (bf_data, 28 KB in this project); BF only keeps one
 *      "bf/<key>" directory entry (24B dirent) in the env KVDB, pointing to an offset in the data area.
 *   2) After commit, fdb_bf_get_addr gives the "XIP absolute address" -- because NOR flash
 *      is mapped into the CPU address space, you can read it directly via (const uint8_t*)xip_addr,
 *      with no memcpy to RAM, ideal for large blobs such as LVGL images/fonts/OTA fragments.
 *
 * Minimal loop demonstrated by this test:
 *   create  -> append three chunks -> commit (CRC optional) -> get_addr -> verify bytes ->
 *   stat    → exists → delete → verify miss.
 */

#include <string.h>
#include <stdint.h>
#include <flashdb.h>
#include "flashdb_registry.h"
#include "flashdb_test.h"

#define LOG_TAG  "[fdb_bf]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

#define BF_TEST_KEY   "test/hello"
#define BF_CHUNK1     "FlashDB BF test - "
#define BF_CHUNK2     "chunk 2 - "
#define BF_CHUNK3     "END"

/* Expected full content (concatenation of the three chunks). Used for read-back verification. */
static const char k_expect[] = BF_CHUNK1 BF_CHUNK2 BF_CHUNK3;

/* ------------------------------------------------------------------ */
/* Subtest 1: full write loop                                           */
/* ------------------------------------------------------------------ */
static int run_write(fdb_bf_t bf)
{
    LOGI("--- create + append*3 + commit ---");

    /* Delete any leftover file with the same name first so the test is re-entrant. */
    (void)fdb_bf_delete(bf, BF_TEST_KEY);

    /* create needs a max_size hint to allocate a contiguous capacity. Here we use the full length
     * +8 bytes for redundancy; the actual allocation is rounded up to the flash sector (4KB). */
    fdb_bf_file_t h = NULL;
    fdb_err_t ret = fdb_bf_create(bf, BF_TEST_KEY, sizeof(k_expect) + 8, &h);
    if (ret != FDB_NO_ERR || h == NULL)
    {
        LOGE("  create failed, ret=%d", (int)ret);
        return -1;
    }
    LOGI("  create: cap=%u", (unsigned)h->capacity);

    /* append the three byte chunks. Any length and any number of appends are allowed. */
    ret = fdb_bf_append(h, BF_CHUNK1, strlen(BF_CHUNK1));
    LOGI("  append #1 (%u B)  %s", (unsigned)strlen(BF_CHUNK1),
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    if (ret != FDB_NO_ERR) { fdb_bf_abort(h); return -2; }

    ret = fdb_bf_append(h, BF_CHUNK2, strlen(BF_CHUNK2));
    LOGI("  append #2 (%u B)  %s", (unsigned)strlen(BF_CHUNK2),
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    if (ret != FDB_NO_ERR) { fdb_bf_abort(h); return -3; }

    /* The last chunk includes the NUL terminator, so the XIP read-back is directly a C string. */
    ret = fdb_bf_append(h, BF_CHUNK3, strlen(BF_CHUNK3) + 1);
    LOGI("  append #3 (%u B, incl. NUL)  %s",
         (unsigned)(strlen(BF_CHUNK3) + 1),
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    if (ret != FDB_NO_ERR) { fdb_bf_abort(h); return -4; }

    /* commit without CRC; BF clears CRC_VALID in flags. To verify, accumulate
     * crc32(buf, len) yourself and commit(&crc) -- this stays a minimal demo. */
    ret = fdb_bf_commit(h, NULL);
    LOGI("  commit  %s", ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    return (ret == FDB_NO_ERR) ? 0 : -5;
}

/* ------------------------------------------------------------------ */
/* Subtest 2: read back and verify via the XIP absolute address         */
/*                                                                    */
/* The addr returned by fdb_bf_get_addr is an absolute flash memory address; once NOR flash is mapped */
/* into the CPU address space it can be cast to (const uint8_t*) and read byte by byte -- that is XIP. */
/* ------------------------------------------------------------------ */
static int run_read_xip(fdb_bf_t bf)
{
    LOGI("--- XIP read-back ---");

    uint32_t xip_addr = 0;
    size_t   size     = 0;
    fdb_err_t ret = fdb_bf_get_addr(bf, BF_TEST_KEY, &xip_addr, &size);
    if (ret != FDB_NO_ERR)
    {
        LOGE("  get_addr failed, ret=%d", (int)ret);
        return -1;
    }
    LOGI("  addr=0x%08x size=%u", (unsigned)xip_addr, (unsigned)size);

    if (size != sizeof(k_expect))
    {
        LOGE("  size mismatch: got=%u expect=%u",
             (unsigned)size, (unsigned)sizeof(k_expect));
        return -2;
    }

    /* Read directly from the flash address space. This is BF's core advantage over KV/TS --
     * KV/TS blobs go through FDB internal header parsing on every read, while BF is one step. */
    const char *data = (const char *)(uintptr_t)xip_addr;
    bool match = (memcmp(data, k_expect, sizeof(k_expect)) == 0);
    LOGI("  content = \"%s\"  %s", data, match ? "[OK]" : "[FAIL]");
    return match ? 0 : -3;
}

/* ------------------------------------------------------------------ */
/* Subtest 3: stat / exists / delete / verify miss                     */
/* ------------------------------------------------------------------ */
static void run_manage(fdb_bf_t bf)
{
    LOGI("--- stat + exists + delete + verify miss ---");

    struct fdb_bf_dirent ent;
    fdb_err_t ret = fdb_bf_stat(bf, BF_TEST_KEY, &ent);
    LOGI("  stat: size=%u cap=%u offset=%u flags=0x%x  %s",
         (unsigned)ent.size, (unsigned)ent.capacity,
         (unsigned)ent.offset, (unsigned)ent.flags,
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");

    bool exists_before = fdb_bf_exists(bf, BF_TEST_KEY);
    LOGI("  exists (before delete): %s  %s",
         exists_before ? "yes" : "no",
         exists_before ? "[OK]" : "[FAIL]");

    ret = fdb_bf_delete(bf, BF_TEST_KEY);
    LOGI("  delete  %s", ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");

    bool exists_after = fdb_bf_exists(bf, BF_TEST_KEY);
    LOGI("  exists (after  delete): %s  %s",
         exists_after ? "yes" : "no",
         !exists_after ? "[OK]" : "[FAIL]");
}

/* ------------------------------------------------------------------ */
/* Public entry                                                       */
/* ------------------------------------------------------------------ */
void flashdb_test_bf_run(void)
{
    LOGI("========== BF test start ==========");
#ifdef FDB_USING_BF
    fdb_bf_t bf = flashdb_registry_get_asset_bf();
    if (bf == NULL) { LOGE("asset bf not ready"); return; }

    if (run_write(bf) != 0)     { LOGE("write phase failed, abort"); return; }
    if (run_read_xip(bf) != 0)  { LOGE("read-back failed, abort");   return; }
    run_manage(bf);
#else
    LOGE("FDB_USING_BF not enabled in fdb_cfg.h; skipping");
#endif
    LOGI("========== BF test end ==========");
}
