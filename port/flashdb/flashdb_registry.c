/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB instance registry — 详见 flashdb_registry.h。
 */

#include "flashdb_registry.h"
#include <flashdb.h>
#include <fal.h>

#define LOG_TAG  "[flashdb_reg]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

#define PEDO_LAYOUT_KEY         "flashdb.pedo_layout"
#define PEDO_LAYOUT_VERSION     2u
#define HEALTH_SYNCED_KEY       "health.synced"

/* 各 db 的就绪标志——单独控制，方便某个 db init 失败时其它仍可用。 */
static bool s_env_kvdb_ready  = false;
static bool s_pedo_tsdb_ready = false;
#ifdef FDB_USING_BF
static bool s_asset_bf_ready  = false;
#endif

static struct fdb_kvdb s_env_kvdb;    /* "env"   @ kvdb      分区 */
static struct fdb_tsdb s_pedo_tsdb;   /* "pedo"  @ fdb_tsdb1 分区 */
#ifdef FDB_USING_BF
static struct fdb_bf   s_asset_bf;    /* dir 在 env kvdb，data @ bf_data 分区 */
#endif

/* FDB_USING_TIMESTAMP_64BIT changes both the TSDB sector header and each log
 * index. FlashDB's magic word does not encode that choice, so a 32-bit layout
 * cannot be detected safely by fdb_tsdb_init(). Keep the migration marker in
 * the independent KVDB and erase only the pedometer partition when needed.
 *
 * The marker is written last. A reset during migration therefore causes the
 * whole operation to be retried on the next boot instead of accepting a
 * partially migrated database. */
static int migrate_pedo_layout(void)
{
    uint32_t stored_version = 0u;
    struct fdb_blob blob;
    size_t got = fdb_kv_get_blob(&s_env_kvdb, PEDO_LAYOUT_KEY,
                                 fdb_blob_make(&blob, &stored_version,
                                               sizeof(stored_version)));
    if (got == sizeof(stored_version) && stored_version == PEDO_LAYOUT_VERSION)
    {
        return 0;
    }

    const struct fal_partition *part = fal_partition_find("fdb_tsdb1");
    if (part == NULL)
    {
        LOGE("pedo layout migration: partition not found");
        return -1;
    }
    if (fal_partition_erase_all(part) < 0)
    {
        LOGE("pedo layout migration: partition erase failed");
        return -1;
    }

    uint32_t synced = 0u;
    if (fdb_kv_set_blob(&s_env_kvdb, HEALTH_SYNCED_KEY,
                        fdb_blob_make(&blob, &synced, sizeof(synced))) != FDB_NO_ERR)
    {
        LOGE("pedo layout migration: reset sync watermark failed");
        return -1;
    }

    uint32_t version = PEDO_LAYOUT_VERSION;
    if (fdb_kv_set_blob(&s_env_kvdb, PEDO_LAYOUT_KEY,
                        fdb_blob_make(&blob, &version, sizeof(version))) != FDB_NO_ERR)
    {
        LOGE("pedo layout migration: save version failed");
        return -1;
    }

    LOGI("pedo TSDB migrated to 64-bit timestamp layout");
    return 0;
}

int flashdb_registry_init(void)
{
    /* 幂等：全部就绪就直接返回。任一未就绪就走后面的补齐流程。 */
    if (s_env_kvdb_ready && s_pedo_tsdb_ready
#ifdef FDB_USING_BF
        && s_asset_bf_ready
#endif
       )
    {
        return 0;
    }

    /* 1. FAL —— 分区表。fal_init 内部有 s_inited 幂等标志，安全反复调。
     *    返回值是分区数量，负值表示错误。 */
    int part_cnt = fal_init();
    if (part_cnt <= 0)
    {
        LOGE("fal_init failed, ret=%d", part_cnt);
        return -1;
    }

    /* 2. KVDB —— "env" 数据库。也是后面 BF 的目录承载者。 */
    if (!s_env_kvdb_ready)
    {
        fdb_err_t ret = fdb_kvdb_init(&s_env_kvdb, "env", "kvdb", NULL, NULL);
        if (ret != FDB_NO_ERR)
        {
            LOGE("fdb_kvdb_init(env) failed, ret=%d", (int)ret);
            return -2;
        }
        s_env_kvdb_ready = true;
    }

    /* 3. TSDB —— "pedo" 数据库。每条记录 max 128 字节；get_time 挂 RTC。 */
    if (!s_pedo_tsdb_ready)
    {
        if (migrate_pedo_layout() != 0)
        {
            return -3;
        }

        fdb_err_t ret = fdb_tsdb_init(&s_pedo_tsdb, "pedo", "fdb_tsdb1",
                                      flashdb_get_time, 128, NULL);
        if (ret != FDB_NO_ERR)
        {
            LOGE("fdb_tsdb_init(pedo) failed, ret=%d", (int)ret);
            return -4;
        }
        s_pedo_tsdb_ready = true;
    }

#ifdef FDB_USING_BF
    /* 4. BF —— "asset" 大文件仓。目录写进 env kvdb（以 "bf/" 为前缀），
     *    数据落在 bf_data 分区。 */
    if (!s_asset_bf_ready)
    {
        fdb_err_t ret = fdb_bf_init(&s_asset_bf, &s_env_kvdb, "bf_data", NULL);
        if (ret != FDB_NO_ERR)
        {
            LOGE("fdb_bf_init(asset) failed, ret=%d", (int)ret);
            return -5;
        }
        s_asset_bf_ready = true;
    }
#endif

#ifdef FDB_USING_BF
    LOGI("FlashDB registry ready (partitions=%d, kvdb=env, tsdb=pedo, bf=asset)", part_cnt);
#else
    LOGI("FlashDB registry ready (partitions=%d, kvdb=env, tsdb=pedo)", part_cnt);
#endif
    return 0;
}

fdb_kvdb_t flashdb_registry_get_env_kvdb(void)
{
    if (!s_env_kvdb_ready && flashdb_registry_init() != 0)
    {
        return NULL;
    }
    return &s_env_kvdb;
}

fdb_tsdb_t flashdb_registry_get_pedo_tsdb(void)
{
    if (!s_pedo_tsdb_ready && flashdb_registry_init() != 0)
    {
        return NULL;
    }
    return &s_pedo_tsdb;
}

#ifdef FDB_USING_BF
fdb_bf_t flashdb_registry_get_asset_bf(void)
{
    if (!s_asset_bf_ready && flashdb_registry_init() != 0)
    {
        return NULL;
    }
    return &s_asset_bf;
}
#endif
