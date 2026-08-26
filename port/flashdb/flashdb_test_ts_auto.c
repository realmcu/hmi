/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB TSDB test — auto timestamp (get_time cb).
 *
 * 时间戳来源：fdb_tsl_append 内部调 db->get_time()，也就是本项目里
 *              fdb_time_rtc.c 提供的 UTC epoch 秒（含 last+1 单调补偿）。
 *
 * 秒级精度导致的现象：连续 append 三条常常撞在同一秒——时间源里做了
 * last+1 补偿，因此你会看到 ts 相邻 +1，是"补偿"的可视化，而非时间源出错。
 *
 * 本文件覆盖的最小闭环：append 3 条 → iter 全量 → query_count 对账。
 */

#include <string.h>
#include <flashdb.h>
#include "flashdb_registry.h"
#include "flashdb_test.h"

#define LOG_TAG  "[fdb_ts_auto]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

/* 演示用小结构：温湿度一条 sample。 */
typedef struct
{
    int32_t temperature_dc;   /* 0.1°C */
    int32_t humidity_dp;      /* 0.1%  */
} env_sample_t;

/* ------------------------------------------------------------------ */
/* Subtest 1: append 3 条 sample —— 时间戳来自 db->get_time 回调        */
/* ------------------------------------------------------------------ */
static int run_append_auto(fdb_tsdb_t tsdb)
{
    LOGI("--- append 3 env samples (auto ts from get_time cb) ---");
    int ok = 0;
    for (int i = 0; i < 3; ++i)
    {
        env_sample_t s = { .temperature_dc = 250 + i, .humidity_dp = 600 + i };
        struct fdb_blob blob;
        fdb_err_t ret = fdb_tsl_append(tsdb, fdb_blob_make(&blob, &s, sizeof(s)));
        LOGI("  append #%d temp=%d.%d humi=%d.%d  %s",
             i,
             s.temperature_dc / 10, s.temperature_dc % 10,
             s.humidity_dp    / 10, s.humidity_dp    % 10,
             ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
        if (ret == FDB_NO_ERR) { ok++; }
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Subtest 2: iter 全部记录并读出（回调式 fdb_tsl_iter）                 */
/* ------------------------------------------------------------------ */
typedef struct
{
    fdb_tsdb_t tsdb;
    int        n;
    int        printed;   /* 只打印前 N 条，避免日志刷屏 */
} iter_ctx_t;

#define TS_DUMP_MAX  10

static bool ts_dump_cb(fdb_tsl_t tsl, void *arg)
{
    iter_ctx_t *ctx = (iter_ctx_t *)arg;
    ctx->n++;
    if (ctx->printed < TS_DUMP_MAX)
    {
        env_sample_t rd = {0};
        struct fdb_blob blob;
        size_t got = fdb_blob_read((fdb_db_t)ctx->tsdb,
                                   fdb_tsl_to_blob(tsl, fdb_blob_make(&blob, &rd, sizeof(rd))));
        if (got == sizeof(rd))
        {
            LOGI("  [%d] ts=%lld temp=%d.%d humi=%d.%d",
                 ctx->n - 1, (long long)tsl->time,
                 rd.temperature_dc / 10, rd.temperature_dc % 10,
                 rd.humidity_dp    / 10, rd.humidity_dp    % 10);
        }
        else
        {
            LOGI("  [%d] ts=%lld size mismatch got=%u expect=%u",
                 ctx->n - 1, (long long)tsl->time,
                 (unsigned)got, (unsigned)sizeof(rd));
        }
        ctx->printed++;
    }
    return false;   /* 继续遍历 */
}

static int run_iter_all(fdb_tsdb_t tsdb)
{
    LOGI("--- iterate all records (dump first %d) ---", TS_DUMP_MAX);
    iter_ctx_t ctx = { .tsdb = tsdb, .n = 0, .printed = 0 };
    fdb_tsl_iter(tsdb, ts_dump_cb, &ctx);
    LOGI("iterated %d record(s)", ctx.n);
    return ctx.n;
}

/* ------------------------------------------------------------------ */
/* Subtest 3: query_count 与 iter 结果对账                              */
/* ------------------------------------------------------------------ */
static void run_query_count(fdb_tsdb_t tsdb, int expect)
{
    LOGI("--- query_count over [0, UINT32_MAX] ---");
    size_t cnt = fdb_tsl_query_count(tsdb, 0, (fdb_time_t)UINT32_MAX, FDB_TSL_WRITE);
    LOGI("query_count = %zu (iter reported %d)  %s",
         cnt, expect,
         (int)cnt == expect ? "[OK]" : "[MISMATCH]");
}

/* ------------------------------------------------------------------ */
/* Public entry                                                       */
/* ------------------------------------------------------------------ */
void flashdb_test_ts_auto_run(void)
{
    LOGI("========== TSDB auto-ts test start ==========");
    fdb_tsdb_t tsdb = flashdb_registry_get_pedo_tsdb();
    if (tsdb == NULL) { LOGE("pedo tsdb not ready"); return; }

    int appended = run_append_auto(tsdb);
    int seen     = run_iter_all(tsdb);
    run_query_count(tsdb, seen);

    LOGI("appended=%d seen=%d (seen may exceed appended if tsdb had prior data)",
         appended, seen);
    LOGI("========== TSDB auto-ts test end ==========");
}
