/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB — string API test (fdb_kv_set / fdb_kv_get).
 *
 * "string" 场景演示：NUL 结尾字符串。写入时 FDB 用 strlen+1 计长，读出
 * 返回内部指针，caller 直接 printf 即可。适合 device_name / wifi_ssid /
 * language 这种"配置文本"。
 *
 * 注意 fdb_kv_get 返回的是内部指针，指向 flash 或缓存——不要 free，也
 * 不保证跨 db 操作后仍然有效。要长期保留请自己拷贝。
 */

#include <string.h>
#include <flashdb.h>
#include "flashdb_registry.h"
#include "flashdb_test.h"

#define LOG_TAG  "[fdb_kv_str]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

/* 一组"配置文本"式 key，覆盖典型使用面。 */
static const struct
{
    const char *key;
    const char *val;
} k_string_kvs[] =
{
    { "device_name", "eBadge-v1.0"    },
    { "wifi_ssid",   "eBadge-AP"      },
    { "lang",        "zh-CN"          },
};

static void run_write_and_readback(fdb_kvdb_t kvdb)
{
    LOGI("--- write & read-back %u string KVs ---",
         (unsigned)(sizeof(k_string_kvs) / sizeof(k_string_kvs[0])));

    for (unsigned i = 0; i < sizeof(k_string_kvs) / sizeof(k_string_kvs[0]); ++i)
    {
        fdb_err_t ret = fdb_kv_set(kvdb, k_string_kvs[i].key, k_string_kvs[i].val);
        LOGI("  set  %-12s = \"%s\"  %s",
             k_string_kvs[i].key, k_string_kvs[i].val,
             ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    }

    for (unsigned i = 0; i < sizeof(k_string_kvs) / sizeof(k_string_kvs[0]); ++i)
    {
        char *got = fdb_kv_get(kvdb, k_string_kvs[i].key);
        bool  match = (got != NULL) && (strcmp(got, k_string_kvs[i].val) == 0);
        LOGI("  get  %-12s = \"%s\"  %s",
             k_string_kvs[i].key, got ? got : "(null)",
             match ? "[OK]" : "[FAIL]");
    }
}

static void run_overwrite(fdb_kvdb_t kvdb)
{
    LOGI("--- overwrite semantics (device_name -> \"eBadge-v2.0\") ---");
    fdb_err_t ret = fdb_kv_set(kvdb, "device_name", "eBadge-v2.0");
    LOGI("  overwrite  %s", ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
    char *got = fdb_kv_get(kvdb, "device_name");
    LOGI("  read back  = \"%s\"  %s",
         got ? got : "(null)",
         (got && strcmp(got, "eBadge-v2.0") == 0) ? "[OK]" : "[FAIL]");
}

static void run_delete(fdb_kvdb_t kvdb)
{
    LOGI("--- delete lang + verify miss ---");
    fdb_kv_del(kvdb, "lang");
    char *got = fdb_kv_get(kvdb, "lang");
    LOGI("  read lang  = %s  %s",
         got ? got : "(null)",
         got == NULL ? "[OK]" : "[FAIL]");
}

void flashdb_test_kv_string_run(void)
{
    LOGI("========== KV string test start ==========");
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL) { LOGE("env kvdb not ready"); return; }

    run_write_and_readback(kvdb);
    run_overwrite(kvdb);
    run_delete(kvdb);

    LOGI("========== KV string test end ==========");
}
