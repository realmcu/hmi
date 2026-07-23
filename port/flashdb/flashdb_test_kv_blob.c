/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB — blob API test (fdb_kv_set_blob / fdb_kv_get_blob).
 *
 * "blob" 场景演示：任意二进制 value（整数、结构体），长度由 caller 显式给。
 * 与 kv_string 的区别：不假设 NUL 结尾、value 可含任意字节。
 */

#include <string.h>
#include <flashdb.h>
#include "flashdb_registry.h"
#include "flashdb_test.h"

#define LOG_TAG  "[fdb_kv_blob]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

/* 演示用的业务结构体（含两个 int）。18/24 字节都行，关键是"非字符串"。 */
typedef struct
{
    int temperature;  /* 0.1°C 单位；253 = 25.3°C */
    int humidity;     /* 0.1%  单位；601 = 60.1% */
} sensor_data_t;

/* ------------------------------------------------------------------ */
/* Subtest 1: integer KV (boot_count) — 每次调用递增 1 并写回          */
/* ------------------------------------------------------------------ */
static void run_boot_count(fdb_kvdb_t kvdb)
{
    struct fdb_blob blob;
    int boot_count = 0;

    LOGI("--- boot_count (int blob) ---");

    fdb_kv_get_blob(kvdb, "boot_count",
                    fdb_blob_make(&blob, &boot_count, sizeof(boot_count)));
    if (blob.saved.len > 0)
    {
        LOGI("read  boot_count = %d", boot_count);
    }
    else
    {
        LOGI("boot_count not found, treat as 0");
        boot_count = 0;
    }

    boot_count++;
    fdb_err_t ret = fdb_kv_set_blob(kvdb, "boot_count",
                                    fdb_blob_make(&blob, &boot_count, sizeof(boot_count)));
    LOGI("write boot_count = %d  %s", boot_count,
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
}

/* ------------------------------------------------------------------ */
/* Subtest 2: struct blob KV (write / read / delete / verify deletion)*/
/* ------------------------------------------------------------------ */
static void run_struct_blob(fdb_kvdb_t kvdb)
{
    struct fdb_blob blob;

    LOGI("--- sensor (struct blob) ---");

    sensor_data_t wr = {.temperature = 253, .humidity = 601};
    fdb_err_t ret = fdb_kv_set_blob(kvdb, "sensor",
                                    fdb_blob_make(&blob, &wr, sizeof(wr)));
    LOGI("write sensor: temp=%d humi=%d  %s",
         wr.temperature, wr.humidity,
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");

    sensor_data_t rd = {0};
    fdb_kv_get_blob(kvdb, "sensor", fdb_blob_make(&blob, &rd, sizeof(rd)));
    LOGI("read  sensor: temp=%d humi=%d  %s",
         rd.temperature, rd.humidity,
         blob.saved.len == sizeof(rd) ? "[OK]" : "[FAIL]");

    fdb_kv_del(kvdb, "sensor");
    fdb_kv_get_blob(kvdb, "sensor", fdb_blob_make(&blob, &rd, sizeof(rd)));
    LOGI("delete+verify sensor: saved.len=%u  %s",
         (unsigned)blob.saved.len,
         blob.saved.len == 0 ? "[OK]" : "[FAIL]");
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                */
/* ------------------------------------------------------------------ */
void flashdb_test_kv_blob_boot_count(void)
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL) { LOGE("env kvdb not ready"); return; }
    run_boot_count(kvdb);
}

void flashdb_test_kv_blob_run(void)
{
    LOGI("========== KV blob test start ==========");
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL) { LOGE("env kvdb not ready"); return; }

    run_boot_count(kvdb);
    run_struct_blob(kvdb);

    LOGI("========== KV blob test end ==========");
}
