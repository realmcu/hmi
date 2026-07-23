/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB test suite — aggregate entry + Zephyr shell command registration.
 *
 * Per-subsystem tests each live in their own translation unit:
 *   - flashdb_test_kv_blob.c
 *   - flashdb_test_kv_string.c
 *   - flashdb_test_ts_auto.c       (append via get_time cb)
 *   - flashdb_test_ts_with_ts.c    (append_with_ts, explicit ts)
 *   - flashdb_test_bf.c
 *
 * This file glues them together for the legacy `fdb test` full-run and adds
 * per-subsystem shell commands. Also preserves the legacy public entries in
 * flashdb_test.h so older callers keep compiling.
 */

#include <flashdb.h>
#include "flashdb_registry.h"
#include "flashdb_test.h"

#define LOG_TAG  "[flashdb_test]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

/* -------------------- Legacy public entries -------------------- */
int flashdb_test_init(void)
{
    return flashdb_registry_init();
}

void flashdb_test_boot_count(void)
{
    /* boot_count 只是 KV blob 测试里的一个小切片；这里直接转发。 */
    flashdb_test_kv_blob_boot_count();
}

/* TSDB 聚合入口：先跑 auto-ts，再跑 with-ts。两者共用同一个 pedo tsdb
 * 实例，last_time 在两次之间单调向前——with-ts 里 base 会看到 auto-ts
 * 留下的值。这是刻意为之：合起来正好演示"回调 → 显式"的语义链。 */
void flashdb_test_ts_run(void)
{
    flashdb_test_ts_auto_run();
    flashdb_test_ts_with_ts_run();
}

void flashdb_test_run(void)
{
    LOGI("========== FlashDB full test start ==========");
    if (flashdb_registry_init() != 0)
    {
        LOGE("registry init failed, abort");
        return;
    }
    flashdb_test_kv_blob_run();
    flashdb_test_kv_string_run();
    flashdb_test_ts_run();
    flashdb_test_bf_run();
    LOGI("========== FlashDB full test end ==========");
}

/* -------------------- Zephyr shell -------------------- */
#ifdef CONFIG_SHELL

#include <zephyr/shell/shell.h>

static int cmd_fdb_init(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    int ret = flashdb_test_init();
    shell_print(sh, "flashdb_registry_init ret=%d", ret);
    return ret;
}

static int cmd_fdb_boot_count(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_boot_count();
    return 0;
}

static int cmd_fdb_test_all(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_run();
    return 0;
}

static int cmd_fdb_kv_blob(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_kv_blob_run();
    return 0;
}

static int cmd_fdb_kv_string(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_kv_string_run();
    return 0;
}

static int cmd_fdb_ts(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_ts_run();
    return 0;
}

static int cmd_fdb_ts_auto(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_ts_auto_run();
    return 0;
}

static int cmd_fdb_ts_with_ts(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_ts_with_ts_run();
    return 0;
}

static int cmd_fdb_bf(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    flashdb_test_bf_run();
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(fdb_cmds,
                               SHELL_CMD(init,       NULL, "Init FAL + KVDB + TSDB + BF",              cmd_fdb_init),
                               SHELL_CMD(boot_count, NULL, "Read & increment boot counter (KV)",       cmd_fdb_boot_count),
                               SHELL_CMD(test,       NULL, "Run all subtests",                         cmd_fdb_test_all),
                               SHELL_CMD(kv_blob,    NULL, "Test KVDB blob API",                       cmd_fdb_kv_blob),
                               SHELL_CMD(kv_string,  NULL, "Test KVDB string API",                     cmd_fdb_kv_string),
                               SHELL_CMD(ts,         NULL, "Test TSDB (auto + with_ts)",               cmd_fdb_ts),
                               SHELL_CMD(ts_auto,    NULL, "Test TSDB append (get_time cb)",           cmd_fdb_ts_auto),
                               SHELL_CMD(ts_with_ts, NULL, "Test TSDB append_with_ts (explicit ts)",   cmd_fdb_ts_with_ts),
                               SHELL_CMD(bf,         NULL, "Test Big-File create/append/commit",       cmd_fdb_bf),
                               SHELL_SUBCMD_SET_END
                              );

SHELL_CMD_REGISTER(fdb, &fdb_cmds, "FlashDB test commands", NULL);

#endif /* CONFIG_SHELL */
