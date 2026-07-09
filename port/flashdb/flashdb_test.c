/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB Functional Test — Generic Implementation
 *
 * Core test logic does not depend on any OS. Logs are output via FDB_PRINT (defined in fdb_cfg.h).
 * Zephyr Shell command registration is wrapped by CONFIG_SHELL conditional compilation;
 * other platforms can directly call the flashdb_test_run() entry function.
 */

#include <string.h>
#include <flashdb.h>
#include <fal.h>
#include "flashdb_test.h"

#define LOG_TAG  "[flashdb_test]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* KVDB instance (static, shared within module) */
/* ------------------------------------------------------------------ */
static struct fdb_kvdb s_kvdb;
static bool            s_kvdb_ready = false;

/* Struct type KV example */
typedef struct
{
    int temperature;  /* Unit: 0.1°C, e.g., 253 = 25.3°C */
    int humidity;     /* Unit: 0.1%,   e.g., 601 = 60.1% */
} sensor_data_t;

/* ------------------------------------------------------------------ */
/* Internal: ensure FAL + KVDB are initialized */
/* ------------------------------------------------------------------ */
static int ensure_init(void)
{
    if (s_kvdb_ready)
    {
        return 0;
    }

    int part_cnt = fal_init();
    if (part_cnt <= 0)
    {
        LOGE("fal_init failed, ret=%d", part_cnt);
        return -1;
    }

    fdb_err_t ret = fdb_kvdb_init(&s_kvdb, "env", "kvdb", NULL, NULL);
    if (ret != FDB_NO_ERR)
    {
        LOGE("fdb_kvdb_init failed, ret=%d", (int)ret);
        return -1;
    }

    s_kvdb_ready = true;
    LOGI("FlashDB init OK, partitions=%d", part_cnt);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sub-test: integer KV (boot count) */
/* ------------------------------------------------------------------ */
static void test_boot_count(void)
{
    struct fdb_blob blob;
    int boot_count = 0;

    LOGI("--- boot_count (int KV) ---");

    fdb_kv_get_blob(&s_kvdb, "boot_count",
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
    fdb_err_t ret = fdb_kv_set_blob(&s_kvdb, "boot_count",
                                    fdb_blob_make(&blob, &boot_count, sizeof(boot_count)));
    LOGI("write boot_count = %d  %s", boot_count,
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");
}

/* ------------------------------------------------------------------ */
/* Sub-test: string KV */
/* ------------------------------------------------------------------ */
static void test_string_kv(void)
{
    LOGI("--- device_name (string KV) ---");

    fdb_err_t ret = fdb_kv_set(&s_kvdb, "device_name", "eBadge-v1.0");
    LOGI("write device_name  %s", ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");

    char *val = fdb_kv_get(&s_kvdb, "device_name");
    LOGI("read  device_name = \"%s\"", val ? val : "(null)");
}

/* ------------------------------------------------------------------ */
/* Sub-test: struct blob KV (write / read / delete / verify) */
/* ------------------------------------------------------------------ */
static void test_struct_kv(void)
{
    struct fdb_blob blob;

    LOGI("--- sensor (struct blob KV) ---");

    sensor_data_t wr = {.temperature = 253, .humidity = 601};
    fdb_err_t ret = fdb_kv_set_blob(&s_kvdb, "sensor",
                                    fdb_blob_make(&blob, &wr, sizeof(wr)));
    LOGI("write sensor: temp=%d humi=%d  %s",
         wr.temperature, wr.humidity,
         ret == FDB_NO_ERR ? "[OK]" : "[FAIL]");

    sensor_data_t rd = {0};
    fdb_kv_get_blob(&s_kvdb, "sensor", fdb_blob_make(&blob, &rd, sizeof(rd)));
    LOGI("read  sensor: temp=%d humi=%d  %s",
         rd.temperature, rd.humidity,
         blob.saved.len == sizeof(rd) ? "[OK]" : "[FAIL]");

    fdb_kv_del(&s_kvdb, "sensor");
    fdb_kv_get_blob(&s_kvdb, "sensor", fdb_blob_make(&blob, &rd, sizeof(rd)));
    LOGI("delete+verify sensor: saved.len=%u  %s",
         (unsigned)blob.saved.len,
         blob.saved.len == 0 ? "[OK]" : "[FAIL]");
}

/* ------------------------------------------------------------------ */
/* Public interface: full test, can be called directly on any platform */
/* ------------------------------------------------------------------ */
int flashdb_test_init(void)
{
    return ensure_init();
}

void flashdb_test_boot_count(void)
{
    if (ensure_init() != 0)
    {
        return;
    }
    test_boot_count();
}

void flashdb_test_run(void)
{
    LOGI("========== FlashDB test start ==========");

    if (ensure_init() != 0)
    {
        return;
    }

    test_boot_count();
    test_string_kv();
    test_struct_kv();

    LOGI("========== FlashDB test end ==========");
}

/* ------------------------------------------------------------------ */
/* Zephyr Shell command registration (Zephyr + CONFIG_SHELL only) */
/* ------------------------------------------------------------------ */
#ifdef CONFIG_SHELL

#include <zephyr/shell/shell.h>

static int cmd_fdb_init(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    int ret = flashdb_test_init();
    if (ret == 0)
    {
        shell_print(sh, "FlashDB already initialized or init OK");
    }
    return ret;
}

static int cmd_fdb_boot_count(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    flashdb_test_boot_count();
    return 0;
}

static int cmd_fdb_test(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    flashdb_test_run();
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(fdb_cmds,
                               SHELL_CMD(init,       NULL, "Init FAL + KVDB",               cmd_fdb_init),
                               SHELL_CMD(boot_count, NULL, "Read & increment boot counter", cmd_fdb_boot_count),
                               SHELL_CMD(test,       NULL, "Run full KVDB test",            cmd_fdb_test),
                               SHELL_SUBCMD_SET_END
                              );

SHELL_CMD_REGISTER(fdb, &fdb_cmds, "FlashDB commands", NULL);

#endif /* CONFIG_SHELL */
