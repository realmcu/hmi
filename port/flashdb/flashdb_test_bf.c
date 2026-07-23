/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB Big-File (BF) test — create + append + commit + XIP read.
 *
 * BF 与 KV/TS 最大的不同：
 *   1) 数据分区独立（bf_data 分区，我们工程里 28 KB），BF 只在 env KVDB
 *      里存一条 "bf/<key>" 的目录项（24B dirent），指向数据区偏移。
 *   2) 提交后可用 fdb_bf_get_addr 拿到"XIP 绝对地址"—— 因为 NOR flash
 *      映射到 CPU 地址空间，你可以 (const uint8_t*)xip_addr 直接读，
 *      不用 memcpy 到 RAM，非常适合 LVGL 图片/字体/OTA 分片这种大 blob。
 *
 * 本测试演示的最小闭环：
 *   create  → append 三片 → commit(带 CRC 可选) → get_addr → 校验字节 →
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

/* 期望的完整内容（三段拼接）。用它做读回校验。 */
static const char k_expect[] = BF_CHUNK1 BF_CHUNK2 BF_CHUNK3;

/* ------------------------------------------------------------------ */
/* Subtest 1: 完整写入闭环                                              */
/* ------------------------------------------------------------------ */
static int run_write(fdb_bf_t bf)
{
    LOGI("--- create + append*3 + commit ---");

    /* 先删掉可能残留的同名文件，测试才可重入。 */
    (void)fdb_bf_delete(bf, BF_TEST_KEY);

    /* create 需要给 max_size 提示，用于分配一段连续容量。此处以完整长度
     * +8 字节做冗余，实际分配会向上对齐到 flash sector（4KB）。 */
    fdb_bf_file_t h = NULL;
    fdb_err_t ret = fdb_bf_create(bf, BF_TEST_KEY, sizeof(k_expect) + 8, &h);
    if (ret != FDB_NO_ERR || h == NULL)
    {
        LOGE("  create failed, ret=%d", (int)ret);
        return -1;
    }
    LOGI("  create: cap=%u", (unsigned)h->capacity);

    /* append 三段字节。可以是任意长度、任意次数。 */
    ret = fdb_bf_append(h, BF_CHUNK1, strlen(BF_CHUNK1));
    LOGI("  append #1 (%u B)  %s", (unsigned)strlen(BF_CHUNK1),
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    if (ret != FDB_NO_ERR) { fdb_bf_abort(h); return -2; }

    ret = fdb_bf_append(h, BF_CHUNK2, strlen(BF_CHUNK2));
    LOGI("  append #2 (%u B)  %s", (unsigned)strlen(BF_CHUNK2),
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    if (ret != FDB_NO_ERR) { fdb_bf_abort(h); return -3; }

    /* 最后一段带上 NUL 结尾——这样 XIP 读回来直接就是 C 字符串。 */
    ret = fdb_bf_append(h, BF_CHUNK3, strlen(BF_CHUNK3) + 1);
    LOGI("  append #3 (%u B, incl. NUL)  %s",
         (unsigned)(strlen(BF_CHUNK3) + 1),
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    if (ret != FDB_NO_ERR) { fdb_bf_abort(h); return -4; }

    /* commit 不带 CRC；BF 会把 flags 里 CRC_VALID 位置 0。想要校验就自己
     * 累加 crc32(buf, len) 再 commit(&crc) —— 这里保持最小演示。 */
    ret = fdb_bf_commit(h, NULL);
    LOGI("  commit  %s", ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    return (ret == FDB_NO_ERR) ? 0 : -5;
}

/* ------------------------------------------------------------------ */
/* Subtest 2: 通过 XIP 绝对地址读回并校验                                */
/*                                                                    */
/* fdb_bf_get_addr 返回的 addr 是绝对 flash 内存地址，NOR flash 映射到 */
/* CPU 地址空间后可以直接强转 (const uint8_t*) 逐字节读——这就是 XIP。 */
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

    /* 直接从 flash 地址空间读。这就是 BF 相对 KV/TS 的核心优势——
     * KV/TS 的 blob 每次读都要经过 FDB 内部头解析，BF 一步到位。 */
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
