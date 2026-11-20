/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB — string API test (fdb_kv_set / fdb_kv_get).
 *
 * "string" scenario demo: NUL-terminated strings. On write FDB sizes the value with strlen+1;
 * on read it returns an internal pointer the caller can printf directly. Suits "config text"
 * such as device_name / wifi_ssid / language.
 *
 * Note fdb_kv_get returns an internal pointer into flash or the cache —— do not free it, and
 * it is not guaranteed to stay valid across db operations. Copy it yourself to keep it around.
 */

#include <string.h>
#include <flashdb.h>
#include "flashdb_registry.h"
#include "flashdb_test.h"

#define LOG_TAG  "[fdb_kv_str]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

/* A set of "config text" style keys covering the typical usage surface. */
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
