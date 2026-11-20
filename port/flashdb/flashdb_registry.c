/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB instance registry -- see flashdb_registry.h for details.
 */

#include "flashdb_registry.h"
#include <flashdb.h>
#include <fal.h>

#define LOG_TAG  "[flashdb_reg]"
#define LOGI(fmt, ...)  FDB_PRINT(LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  FDB_PRINT(LOG_TAG " [ERR] " fmt "\n", ##__VA_ARGS__)

#define PEDO_LAYOUT_KEY         "flashdb.pedo_layout"
#define PEDO_LAYOUT_VERSION     3u
#define HEALTH_SYNCED_KEY       "health.synced"

/* Per-db ready flags, controlled individually so others remain usable if one db init fails. */
static bool s_env_kvdb_ready  = false;
static bool s_pedo_tsdb_ready = false;
#ifdef FDB_USING_BF
static bool s_asset_bf_ready  = false;
#endif

static struct fdb_kvdb s_env_kvdb;    /* "env"   @ kvdb      partition */
static struct fdb_tsdb s_pedo_tsdb;   /* "pedo"  @ fdb_tsdb1 partition */
#ifdef FDB_USING_BF
static struct fdb_bf   s_asset_bf;    /* dir in env kvdb, data @ bf_data partition */
#endif

/* The pedometer layout marker covers storage format and record timestamp
 * semantics. Version 2 enabled FlashDB 64-bit timestamps; version 3 changes
 * sport timestamps from closing instants to natural-quarter bucket starts.
 * Keep the marker in the independent KVDB and erase only the pedometer
 * partition when either kind of incompatible change is introduced.
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

    LOGI("pedo TSDB migrated to layout version %u",
         (unsigned)PEDO_LAYOUT_VERSION);
    return 0;
}

int flashdb_registry_init(void)
{
    /* Idempotent: return directly when all are ready; otherwise run the completion flow below. */
    if (s_env_kvdb_ready && s_pedo_tsdb_ready
#ifdef FDB_USING_BF
        && s_asset_bf_ready
#endif
       )
    {
        return 0;
    }

    /* 1. FAL -- partition table. fal_init has an internal s_inited idempotency flag, safe to call repeatedly.
     *    Return value is the partition count; negative indicates an error. */
    int part_cnt = fal_init();
    if (part_cnt <= 0)
    {
        LOGE("fal_init failed, ret=%d", part_cnt);
        return -1;
    }

    /* 2. KVDB -- the "env" database. It also hosts the BF directory used later. */
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

    /* 3. TSDB -- the "pedo" database. Each record is max 128 bytes; get_time is hooked to the RTC. */
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
    /* 4. BF -- the "asset" big-file store. The directory is written into env kvdb (prefixed with "bf/"),
     *    and the data lands in the bf_data partition. */
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
