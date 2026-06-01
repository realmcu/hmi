/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB 功能测试 —— 通用实现
 *
 * 核心测试逻辑不依赖任何 OS，日志通过 FDB_PRINT 输出（在 fdb_cfg.h 中定义）。
 * Zephyr Shell 命令注册通过 CONFIG_SHELL 条件编译包裹，其他平台可直接调用
 * flashdb_test_run() 入口函数。
 */

#include <string.h>
#include <flashdb.h>
#include <fal.h>
#include "flashdb_test.h"

#define LOG_TAG  "[flashdb_test]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* KVDB 实例（静态，模块内共享）                                        */
/* ------------------------------------------------------------------ */
static struct fdb_kvdb s_kvdb;
static bool            s_kvdb_ready = false;

/* 结构体类型 KV 示例 */
typedef struct
{
    int temperature;  /* 单位：0.1℃，例 253 = 25.3℃ */
    int humidity;     /* 单位：0.1%，  例 601 = 60.1% */
} sensor_data_t;

/* ------------------------------------------------------------------ */
/* 内部：确保 FAL + KVDB 已初始化                                       */
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
/* 子测试：整型 KV（掉电计数）                                          */
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
/* 子测试：字符串 KV                                                    */
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
/* 子测试：结构体 blob KV（写 / 读 / 删 / 验证）                        */
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
/* 公开接口：完整测试，任意平台均可直接调用                              */
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
/* Zephyr Shell 命令注册（仅 Zephyr + CONFIG_SHELL 环境）               */
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
