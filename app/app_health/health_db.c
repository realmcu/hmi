/*
 * health_db.c
 *
 * Thin facade in front of FlashDB for the health module. Handles two
 * pieces of state:
 *
 *   1. pedo TSDB — every flushed 15-minute bucket is appended as one
 *      health_pedo_record_t. Time key is the record's ts_utc (same value
 *      the RTC-backed flashdb_get_time returns), so time-range queries
 *      done through fdb_tsl_iter_by_time match record content exactly.
 *
 *   2. env KVDB, key "health.today" — the accumulated {steps, distance,
 *      calories} for the current UTC day. Lets a UI wake-up read today's
 *      totals in one KV get instead of iterating the TSDB. Automatically
 *      treated as zero when the stored utc_day_index disagrees with the
 *      caller's "today" — i.e. day rollover is handled at read time so
 *      no timer has to fire exactly at midnight.
 *
 * All handles are cached lazily via flashdb_registry_*; if the registry
 * fails to bring the DB up, every function here degrades to a
 * "return -EIO" no-op so the health module can log and carry on.
 */

#include "app_health_internal.h"
#include "app_log.h"

#include <flashdb.h>
#include "flashdb_registry.h"

#include <errno.h>
#include <string.h>
#include <os_sync.h>

APP_LOG_MODULE_REGISTER(health_db);

#define HEALTH_TODAY_KEY   "health.today"

static void *s_health_db_lock;

static void db_lock(void)
{
    (void)os_mutex_take(s_health_db_lock, 0xFFFFFFFFu);
}

static void db_unlock(void)
{
    (void)os_mutex_give(s_health_db_lock);
}

int health_db_init(void)
{
    if (s_health_db_lock == NULL && !os_mutex_create(&s_health_db_lock))
    {
        APP_LOGE("health db mutex create failed");
        return -ENOMEM;
    }

    /* Registry init is idempotent; calling once here surfaces the failure
     * early instead of on the first append. Downstream getters still
     * lazy-check, so a late recovery (e.g. flash driver bring-up race)
     * would still work. */
    int rc = flashdb_registry_init();
    if (rc != 0)
    {
        APP_LOGE("flashdb_registry_init failed rc=%d", rc);
        return rc;
    }
    return 0;
}

int health_db_append_pedo(health_pedo_record_t *rec)
{
    if (rec == NULL)
    {
        return -EINVAL;
    }

    fdb_tsdb_t tsdb = flashdb_registry_get_pedo_tsdb();
    if (tsdb == NULL)
    {
        APP_LOGE("pedo tsdb not ready");
        return -EIO;
    }

    struct fdb_blob blob;
    db_lock();
    fdb_time_t last_time = 0;
    fdb_tsdb_control(tsdb, FDB_TSDB_CTRL_GET_LAST_TIME, &last_time);
    if ((fdb_time_t)rec->ts_utc < last_time || last_time >= INT32_MAX)
    {
        db_unlock();
        APP_LOGE("pedo timestamp rollback now=%u last=%d",
                 (unsigned)rec->ts_utc, (int)last_time);
        return -ERANGE;
    }
    if ((fdb_time_t)rec->ts_utc == last_time)
    {
        rec->ts_utc++;
    }
    fdb_err_t e = fdb_tsl_append_with_ts(tsdb,
                                         fdb_blob_make(&blob, rec, sizeof(*rec)),
                                         (fdb_time_t)rec->ts_utc);
    db_unlock();
    if (e != FDB_NO_ERR)
    {
        APP_LOGE("fdb_tsl_append err=%d", (int)e);
        return -EIO;
    }
    return 0;
}

int health_db_load_today(uint32_t current_utc_day_index,
                         health_daily_rollup_t *out)
{
    if (out == NULL)
    {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    out->utc_day_index = current_utc_day_index;

    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL)
    {
        APP_LOGE("env kvdb not ready");
        return -EIO;
    }

    health_daily_rollup_t stored;
    struct fdb_blob blob;
    db_lock();
    size_t got = fdb_kv_get_blob(kvdb, HEALTH_TODAY_KEY,
                                 fdb_blob_make(&blob, &stored, sizeof(stored)));
    db_unlock();
    if (got == 0)
    {
        /* First boot / KV wiped: today starts at zero, already zeroed above. */
        return 0;
    }
    if (got != sizeof(stored))
    {
        /* Struct grew or shrank between firmware versions — refuse to trust
         * the record but don't propagate the failure; the caller carries on
         * with today=0, and the next flush overwrites the stale entry. */
        APP_LOGW("today KV size mismatch got=%u expect=%u; discarding",
                 (unsigned)got, (unsigned)sizeof(stored));
        return 0;
    }

    if (stored.utc_day_index != current_utc_day_index)
    {
        /* Stored snapshot belongs to a previous UTC day. Not an error —
         * "today" is empty by definition. Leave out zeroed with the new
         * utc_day_index already set. */
        return 0;
    }

    *out = stored;
    return 0;
}

int health_db_save_today(const health_daily_rollup_t *rollup)
{
    if (rollup == NULL)
    {
        return -EINVAL;
    }

    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL)
    {
        return -EIO;
    }

    struct fdb_blob blob;
    db_lock();
    fdb_err_t e = fdb_kv_set_blob(kvdb, HEALTH_TODAY_KEY,
                                  fdb_blob_make(&blob, rollup, sizeof(*rollup)));
    db_unlock();
    if (e != FDB_NO_ERR)
    {
        APP_LOGE("fdb_kv_set_blob(today) err=%d", (int)e);
        return -EIO;
    }
    return 0;
}

/* Bridge from FDB's callback (fdb_tsl_t, void*) to our own callback that
 * hands out an already-decoded record. This keeps the caller from having
 * to know about FDB blob mechanics. */
typedef struct
{
    health_db_iter_cb_t     user_cb;
    void                   *user_arg;
    fdb_tsdb_t              tsdb;
    size_t                  visited;
    bool                    stop;
} iter_ctx_t;

static bool pedo_iter_adapter(fdb_tsl_t tsl, void *arg)
{
    iter_ctx_t *ctx = (iter_ctx_t *)arg;
    if (ctx->stop)
    {
        return true; /* tell FDB to stop */
    }

    health_pedo_record_t rec;
    struct fdb_blob blob;
    size_t got = fdb_blob_read((fdb_db_t)ctx->tsdb,
                               fdb_tsl_to_blob(tsl,
                                               fdb_blob_make(&blob, &rec, sizeof(rec))));
    if (got != sizeof(rec))
    {
        /* Skip a size-mismatched entry (probably an older-firmware record)
         * without aborting the iteration — the caller may still want the
         * newer ones. */
        return false;
    }

    ctx->visited++;
    if (ctx->user_cb != NULL &&
        ctx->user_cb(&rec, (uint32_t)tsl->time, (uint32_t)tsl->addr.index,
                     ctx->user_arg))
    {
        ctx->stop = true;
        return true;
    }
    return false;
}

size_t health_db_iter(uint32_t from, uint32_t to,
                      health_db_iter_cb_t cb, void *user)
{
    fdb_tsdb_t tsdb = flashdb_registry_get_pedo_tsdb();
    if (tsdb == NULL)
    {
        return 0;
    }

    iter_ctx_t ctx =
    {
        .user_cb  = cb,
        .user_arg = user,
        .tsdb     = tsdb,
        .visited  = 0,
        .stop     = false,
    };

    db_lock();
    if (from == 0 && to == 0)
    {
        fdb_tsl_iter(tsdb, pedo_iter_adapter, &ctx);
    }
    else
    {
        fdb_time_t hi = (to == 0) ? (fdb_time_t)0x7FFFFFFF : (fdb_time_t)to;
        fdb_tsl_iter_by_time(tsdb, (fdb_time_t)from, hi,
                             pedo_iter_adapter, &ctx);
    }
    db_unlock();
    return ctx.visited;
}

size_t health_db_count(uint32_t from, uint32_t to)
{
    return health_db_iter(from, to, NULL, NULL);
}

int health_db_clean(void)
{
    fdb_tsdb_t tsdb = flashdb_registry_get_pedo_tsdb();
    if (tsdb == NULL)
    {
        return -EIO;
    }
    db_lock();
    fdb_tsl_clean(tsdb);
    db_unlock();
    return 0;
}
