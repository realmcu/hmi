/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB TSDB test — explicit timestamp via fdb_tsl_append_with_ts.
 *
 * Typical uses: backfilling history (old records pushed up by the APP), syncing records that
 * already carry a ts from an external device, data migration. Semantically "the caller owns
 * monotonicity" —— the ts passed in must be strictly greater than db->last_time, else FDB returns FDB_WRITE_ERR.
 *
 * This file demonstrates these paths:
 *   Subtest 1: normal path —— 3 strictly increasing ts values, all written successfully
 *   Subtest 2: monotonicity violation —— pass a ts equal to last_time, rejected by FDB (expected)
 *   Subtest 3: iter over everything and read the content back
 *   Subtest 4: query_count reconciliation
 *
 * Note: this test picks "base + i+1" as the ts (base = the current last_time), so the records
 * written are "appended at the end of the timeline", not "backfilled into the past". To really
 * demonstrate "backfilling history" you would first have to fdb_tsl_clean the db, which would
 * also wipe the auto-ts batch, so it is left to a manual `fdb clean` from the shell.
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
/* Subtest 1: 3 strictly increasing ts values, all writes succeed      */
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

    /* Record the last_time at the end of this round, for subtest 2. */
    fdb_tsdb_control(tsdb, FDB_TSDB_CTRL_GET_LAST_TIME, out_last);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Subtest 2: monotonicity violation — should be rejected by FDB (cur <= last) */
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
/* Subtest 3: iterate all records and read them back                   */
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
/* Subtest 4: reconcile query_count against the iter result            */
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
