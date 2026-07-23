/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB TSDB test — explicit timestamp via fdb_tsl_append_with_ts.
 *
 * 典型用途：补历史数据（APP 灌上传的老记录）、从外部设备同步已带 ts 的
 * 记录、数据迁移。语义上"caller 自己负责单调"——传入的 ts 必须严格大于
 * db->last_time，否则 FDB 返回 FDB_WRITE_ERR。
 *
 * 本文件同时演示两条路径：
 *   Subtest 1: 正常路径 —— 3 条 ts 严格递增，全部写入成功
 *   Subtest 2: 单调违规 —— 传入 last_time 相等的 ts，被 FDB 拒收（预期）
 *   Subtest 3: iter 全量对读回内容
 *   Subtest 4: query_count 对账
 *
 * 注：本测试选择"base + i+1"作为 ts（base = 当前 last_time），因此写进
 * 去的记录是"追加在时间线末尾"，不是"补录到过去"。要真正演示"补历史"
 * 需要先 fdb_tsl_clean 清空 db，那样会把 auto-ts 那一批也擦掉，所以留
 * 给 shell 手动 `fdb clean` 后自己试。
 */

#include <string.h>
#include <flashdb.h>
#include "flashdb_registry.h"
#include "flashdb_test.h"

#define LOG_TAG  "[fdb_ts_wts]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

typedef struct
{
    int32_t temperature_dc;
    int32_t humidity_dp;
} env_sample_t;

/* ------------------------------------------------------------------ */
/* Subtest 1: 3 条 ts 严格递增，写入成功                                 */
/* ------------------------------------------------------------------ */
static int run_backfill_ok(fdb_tsdb_t tsdb, fdb_time_t *out_last)
{
    LOGI("--- append_with_ts x3 (strictly increasing) ---");

    fdb_time_t base = 0;
    fdb_tsdb_control(tsdb, FDB_TSDB_CTRL_GET_LAST_TIME, &base);
    LOGI("  current last_time = %lld", (long long)base);

    int ok = 0;
    for (int i = 0; i < 3; ++i)
    {
        env_sample_t s = { .temperature_dc = 300 + i, .humidity_dp = 550 + i };
        struct fdb_blob blob;

        fdb_time_t ts = base + (fdb_time_t)(i + 1);
        fdb_err_t ret = fdb_tsl_append_with_ts(tsdb,
                                               fdb_blob_make(&blob, &s, sizeof(s)),
                                               ts);
        LOGI("  append_with_ts #%d ts=%lld temp=%d.%d humi=%d.%d  %s",
             i, (long long)ts,
             s.temperature_dc / 10, s.temperature_dc % 10,
             s.humidity_dp    / 10, s.humidity_dp    % 10,
             ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
        if (ret == FDB_NO_ERR) { ok++; }
    }

    /* 记录本轮结束时的 last_time，供 subtest 2 用。 */
    fdb_tsdb_control(tsdb, FDB_TSDB_CTRL_GET_LAST_TIME, out_last);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Subtest 2: 单调违规 —— 应该被 FDB 拒收（cur <= last）                  */
/* ------------------------------------------------------------------ */
static void run_monotonic_violation(fdb_tsdb_t tsdb, fdb_time_t last)
{
    LOGI("--- monotonic violation: append_with_ts(ts == last_time) ---");

    env_sample_t s = { .temperature_dc = 999, .humidity_dp = 999 };
    struct fdb_blob blob;

    fdb_err_t ret = fdb_tsl_append_with_ts(tsdb,
                                           fdb_blob_make(&blob, &s, sizeof(s)),
                                           last);
    LOGI("  intentional duplicate ts=%lld  ret=%d  %s",
         (long long)last, (int)ret,
         ret != FDB_NO_ERR
         ? "[OK — rejected as expected]"
         : "[FAIL — should have been rejected]");
}

/* ------------------------------------------------------------------ */
/* Subtest 3: iter 全部记录并读出                                       */
/* ------------------------------------------------------------------ */
typedef struct
{
    fdb_tsdb_t tsdb;
    int        n;
    int        printed;
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
    return false;
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
/* Subtest 4: query_count 与 iter 结果对账                              */
/* ------------------------------------------------------------------ */
static void run_query_count(fdb_tsdb_t tsdb, int expect)
{
    LOGI("--- query_count over [0, INT32_MAX] ---");
    size_t cnt = fdb_tsl_query_count(tsdb, 0, (fdb_time_t)0x7FFFFFFF, FDB_TSL_WRITE);
    LOGI("query_count = %zu (iter reported %d)  %s",
         cnt, expect,
         (int)cnt == expect ? "[OK]" : "[MISMATCH]");
}

/* ------------------------------------------------------------------ */
/* Public entry                                                       */
/* ------------------------------------------------------------------ */
void flashdb_test_ts_with_ts_run(void)
{
    LOGI("========== TSDB with-ts test start ==========");
    fdb_tsdb_t tsdb = flashdb_registry_get_pedo_tsdb();
    if (tsdb == NULL) { LOGE("pedo tsdb not ready"); return; }

    fdb_time_t last_after_bf = 0;
    int appended = run_backfill_ok(tsdb, &last_after_bf);
    run_monotonic_violation(tsdb, last_after_bf);
    int seen = run_iter_all(tsdb);
    run_query_count(tsdb, seen);

    LOGI("appended_with_ts=%d seen=%d", appended, seen);
    LOGI("========== TSDB with-ts test end ==========");
}
