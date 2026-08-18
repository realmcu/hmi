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
 *   2. env KVDB — two fixed-size values under known keys:
 *      "health.today"  the accumulated {steps, distance, calories} for the
 *                      current UTC day, so a UI wake-up reads today's totals
 *                      in one KV get instead of iterating the TSDB. Treated
 *                      as zero when the stored utc_day_index disagrees with
 *                      the caller's "today", i.e. day rollover is handled at
 *                      read time and no timer has to fire at midnight.
 *      "health.synced" the sequential-read watermark: ts_utc of the newest
 *                      record handed to a consumer. Lives here rather than in
 *                      the app layer because it relies on the strictly
 *                      increasing timestamps this file enforces on append.
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
#define HEALTH_SYNCED_KEY  "health.synced"

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

/* --------------------------------------------------------------
 * env KV helpers. Both stored values (today rollup, sync watermark) are
 * fixed-size blobs under a known key, so the get-handle / lock / blob /
 * unlock shape is shared and only the interpretation differs.
 * -------------------------------------------------------------- */

/* Read a blob of exactly @c len bytes. Returns 1 when @c out was filled, 0
 * when the key is absent or stored at a different size (caller treats that as
 * "no value"), negative when the store itself is unusable. */
static int kv_load(const char *key, void *out, size_t len)
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL)
    {
        APP_LOGE("env kvdb not ready");
        return -EIO;
    }

    struct fdb_blob blob;
    db_lock();
    size_t got = fdb_kv_get_blob(kvdb, key, fdb_blob_make(&blob, out, len));
    db_unlock();

    if (got == 0)
    {
        return 0;                 /* first boot / KV wiped */
    }
    if (got != len)
    {
        /* Layout changed between firmware versions. Refuse to trust the entry
         * rather than reinterpret its bytes; the next save overwrites it. */
        APP_LOGW("KV '%s' size mismatch got=%u expect=%u; discarding",
                 key, (unsigned)got, (unsigned)len);
        return 0;
    }
    return 1;
}

static int kv_save(const char *key, const void *val, size_t len)
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL)
    {
        return -EIO;
    }

    struct fdb_blob blob;
    db_lock();
    fdb_err_t e = fdb_kv_set_blob(kvdb, key, fdb_blob_make(&blob, val, len));
    db_unlock();
    if (e != FDB_NO_ERR)
    {
        APP_LOGE("fdb_kv_set_blob('%s') err=%d", key, (int)e);
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

    health_daily_rollup_t stored;
    int rc = kv_load(HEALTH_TODAY_KEY, &stored, sizeof(stored));
    if (rc <= 0)
    {
        /* Absent, stale-sized, or unusable store: "today so far = 0" is still
         * a valid answer, so only a store failure propagates. */
        return (rc < 0) ? rc : 0;
    }

    /* A snapshot from a previous UTC day is not an error — today is empty by
     * definition, and out is already zeroed with the new day index. */
    if (stored.utc_day_index == current_utc_day_index)
    {
        *out = stored;
    }
    return 0;
}

int health_db_save_today(const health_daily_rollup_t *rollup)
{
    if (rollup == NULL)
    {
        return -EINVAL;
    }
    return kv_save(HEALTH_TODAY_KEY, rollup, sizeof(*rollup));
}

static int health_db_load_synced_ts(uint32_t *out_ts)
{
    if (out_ts == NULL)
    {
        return -EINVAL;
    }
    *out_ts = 0u;

    uint32_t stored = 0u;
    int rc = kv_load(HEALTH_SYNCED_KEY, &stored, sizeof(stored));
    if (rc <= 0)
    {
        /* Nothing synced yet, or an entry we refuse to trust: re-sending
         * history the phone may already hold is safe (it de-dupes), whereas
         * trusting a bad watermark would skip records forever. */
        return (rc < 0) ? rc : 0;
    }

    *out_ts = stored;
    return 0;
}

static int health_db_save_synced_ts(uint32_t ts_utc)
{
    return kv_save(HEALTH_SYNCED_KEY, &ts_utc, sizeof(ts_utc));
}

/* Bridge from FDB's callback (fdb_tsl_t, void*) to one that hands out an
 * already-decoded record, so the consumer never sees FDB blob mechanics.
 * Returning true from the callback stops the iteration. */
typedef bool (*health_db_iter_cb_t)(const health_pedo_record_t *rec, void *user);

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
    if (ctx->user_cb != NULL && ctx->user_cb(&rec, ctx->user_arg))
    {
        ctx->stop = true;
        return true;
    }
    return false;
}

/* Iterate records with ts_utc in [from, to]; @c to == 0 means no upper bound.
 * Returns the number of records visited. Private now that the sequential
 * reader below is the only consumer. */
static size_t health_db_iter(uint32_t from, uint32_t to,
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

/* --------------------------------------------------------------
 * Sequential history reads.
 *
 * A TSDB iteration cannot be suspended and resumed, so "where did a consumer
 * get to" is tracked here as a watermark: the ts_utc of the newest record
 * handed out. That is sound only because health_db_append_pedo() guarantees
 * strictly increasing timestamps — the same invariant this file enforces, so
 * the cursor belongs next to it.
 *
 * The read API is per-record but flash access is batched: re-entering the
 * iteration per record costs ~4x the flash reads and gives the acquisition
 * worker that many windows to append (and roll over) mid-read. The watermark
 * advances only over records actually DELIVERED, so abandoning a read
 * mid-batch loses nothing.
 *
 * Single reader by design: the BLE spec allows one sync session per link, and
 * health_db_iter() is untouched by this, so `health list` still sees
 * everything regardless of how much has been synced.
 * -------------------------------------------------------------- */

#define HISTORY_PREFETCH  8u

static struct
{
    bool     loaded;                          /* watermark read from KV yet? */
    uint32_t synced_ts;                       /* newest ts_utc handed out    */
    health_pedo_record_t buf[HISTORY_PREFETCH];
    uint8_t  n;                               /* records in buf              */
    uint8_t  taken;                           /* delivered out of buf        */
} s_hist;

typedef struct
{
    health_pedo_record_t *buf;
    size_t                n;
} prefetch_fill_t;

static bool prefetch_fill_cb(const health_pedo_record_t *rec, void *user)
{
    prefetch_fill_t *fill = (prefetch_fill_t *)user;

    fill->buf[fill->n++] = *rec;

    return fill->n >= HISTORY_PREFETCH;
}

/* Pull the next batch into s_hist.buf. Returns records fetched, or -errno. */
static int history_refill(void)
{
    if (health_db_init() != 0)
    {
        return -EIO;
    }

    /* Loaded lazily so a KV read never sits on the module's startup path. */
    if (!s_hist.loaded)
    {
        (void)health_db_load_synced_ts(&s_hist.synced_ts);
        s_hist.loaded = true;
    }

    prefetch_fill_t fill =
    {
        .buf = s_hist.buf,
        .n   = 0u,
    };

    /* synced_ts is the last record already handed out, so start one second
     * past it. Zero means nothing was ever synced: start at the oldest
     * record, which health_db_iter spells as from == 0. */
    uint32_t from = (s_hist.synced_ts == 0u) ? 0u : s_hist.synced_ts + 1u;
    (void)health_db_iter(from, 0u, prefetch_fill_cb, &fill);

    s_hist.n     = (uint8_t)fill.n;
    s_hist.taken = 0u;

    return (int)fill.n;
}

int health_db_read_next(health_pedo_record_t *out)
{
    if (out == NULL)
    {
        return -EINVAL;
    }

    if (s_hist.taken >= s_hist.n)
    {
        /* A short batch last time means the store was exhausted then; retry
         * anyway, since the worker may have appended a bucket since. */
        int rc = history_refill();
        if (rc <= 0)
        {
            return rc;
        }
    }

    *out = s_hist.buf[s_hist.taken++];

    /* Advance the watermark over what was actually delivered, and persist once
     * per drained batch rather than per record: a full 476-record sync then
     * costs ~60 KV writes instead of 476. Losing a batch's tail to a power cut
     * just re-sends those records, which the phone de-dupes. */
    s_hist.synced_ts = out->ts_utc;
    if (s_hist.taken >= s_hist.n && health_db_save_synced_ts(s_hist.synced_ts) != 0)
    {
        APP_LOGW("synced watermark save failed (will re-send on reboot)");
    }

    return 1;
}
