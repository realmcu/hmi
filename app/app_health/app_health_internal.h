#ifndef __APP_HEALTH_INTERNAL_H__
#define __APP_HEALTH_INTERNAL_H__

/*
 * app_health_internal.h
 *
 * Constants + record layout + cross-file (health_worker / health_db /
 * app_health) plumbing that stays inside the health module. Nothing here
 * is intended for consumers outside app/app_health/.
 *
 * Ownership summary
 * -----------------
 *  - health_worker.c : owns the private acquisition task, the gsa FSM,
 *                      the accumulator and the bucket deadline. Calls
 *                      into health_db on flush.
 *  - health_db.c     : owns the pedo TSDB + env KV writes; provides
 *                      read-only queries used by the shell.
 *  - app_health.c    : module lifecycle. Wires events (EVT_TIME_SYNCED
 *                      arms the worker, EVT_POWER_LOW disarms it) and
 *                      republishes flushed totals as EVT_HEALTH_STEPS_UPDATED.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------
 * Product-fixed constants — NOT tunable at runtime.
 *
 * The 15-minute bucket comes from product spec; changing it invalidates
 * every historical record already on flash. Reserved as a compile-time
 * constant so nothing at runtime (settings, shell, BLE) can rewrite it.
 * -------------------------------------------------------------- */
#define HEALTH_BUCKET_MIN        15u
#define HEALTH_BUCKET_SEC        (HEALTH_BUCKET_MIN * 60u)

#define HEALTH_ODR_HZ            25u

/* Debug input source: keep the sensor read as the 25 Hz pacing source, but
 * feed the embedded SD_001 Q9 trace into GSA instead of live sensor data. */
#define HEALTH_USE_SD001_SAMPLE  1

/* OSIF uses larger numbers for higher priorities. Keep the worker below
 * app_task (priority 3) so sampling cannot preempt event dispatch. */
#define HEALTH_WORKER_PRIO       2
#define HEALTH_WORKER_STACK      2048u

#define HEALTH_GS_PATH           "/dev/gsensor0"
#define HEALTH_RTC_PATH          "/dev/rtc0"

/* --------------------------------------------------------------
 * On-flash record layout (18 B).
 *
 * The exact byte layout is part of the storage ABI: TSDB records already
 * on the device from a previous firmware version must still parse. Fields
 * are integer-only (no float / no host-endianness assumptions beyond
 * "little-endian ARM") and struct is packed to keep header ↔ payload math
 * stable across compiler versions.
 * -------------------------------------------------------------- */
typedef struct
{
    uint32_t ts_utc;        /* UTC epoch seconds captured at flush moment  */
    uint16_t steps;         /* clipped to 0xFFFF                            */
    uint16_t distance_m;    /* metres, clipped to 0xFFFF                    */
    uint16_t calories_dkcal; /* 0.1 kcal units, clipped to 0xFFFF           */
    uint8_t  hr_avg;        /* bpm — 0 when hr not implemented yet          */
    uint8_t  bucket_min;    /* always HEALTH_BUCKET_MIN today, kept for     */
    /*  forward-compat if product ever changes it   */
    uint8_t  mode;          /* 0=walk 1=run 2=invalid (gsa_pedo_info.mode)  */
    uint8_t  flags;         /* bit0=has_hr, others reserved                 */
    uint32_t reserved;      /* pad to 18 bytes; future field growth room    */
} __attribute__((packed)) health_pedo_record_t;

_Static_assert(sizeof(health_pedo_record_t) == 18,
               "health_pedo_record_t must stay 18 bytes; flash layout depends on it");

/* --------------------------------------------------------------
 * Aggregated view of "today so far" — cached in env KVDB under
 * "health.today" so UI wake-up doesn't have to iterate the TSDB.
 *
 * Not written on every sample; updated once per successful flush and
 * zero'd when the UTC day rolls over (or when the store detects a
 * stale record on load).
 * -------------------------------------------------------------- */
typedef struct
{
    uint32_t utc_day_index; /* floor(ts_utc / 86400) at last update       */
    uint32_t steps;
    uint32_t distance_m;
    uint32_t calories_dkcal;
} health_daily_rollup_t;

/* --------------------------------------------------------------
 * health_db — TSDB / KV facade.
 *
 * Callers never touch flashdb_registry_* directly; all flash IO for the
 * health module funnels through here so the acquisition path stays
 * unaware of database types.
 * -------------------------------------------------------------- */

/* Lazy-init the underlying flashdb registry. Safe to call more than once.
 * Returns 0 on success, negative if the store is unusable (in which case
 * every other health_db_* call is a no-op returning failure). */
int  health_db_init(void);

/* Append one 18B pedometer record to the TSDB, using the record's ts_utc as
 * the time key. Returns 0 on success, negative on failure. */
int  health_db_append_pedo(const health_pedo_record_t *rec);

/* Load "health.today" from env KV. If no entry exists, or the stored day
 * doesn't match @c current_utc_day_index, zeros @c out and returns 0 anyway — the caller
 * still gets a valid "today so far = 0" view. Returns negative only on
 * unrecoverable flash errors. */
int  health_db_load_today(uint32_t current_utc_day_index,
                          health_daily_rollup_t *out);

/* Persist "health.today" back to env KV. Overwrites any previous value.
 * Returns 0 on success, negative on failure. */
int  health_db_save_today(const health_daily_rollup_t *rollup);

/* Iterate every TSDB record whose ts_utc falls in [from, to]. @c to may be
 * 0 to mean "no upper bound". The callback receives the copied record plus
 * the TSDB-side timestamp (for cross-check); returning false continues the
 * iteration, true stops it. Returns the number of records visited. */
typedef bool (*health_db_iter_cb_t)(const health_pedo_record_t *rec,
                                    uint32_t fdb_ts,
                                    uint32_t fdb_addr,
                                    void *user);
size_t health_db_iter(uint32_t from, uint32_t to,
                      health_db_iter_cb_t cb, void *user);

/* Count TSDB records with ts_utc in [from, to] and status = WRITE. */
size_t health_db_count(uint32_t from, uint32_t to);

/* Erase every record in the pedo TSDB. Does NOT touch the today-KV; caller
 * should zero that separately if needed. Returns 0 on success. */
int  health_db_clean(void);

/* --------------------------------------------------------------
 * health_worker — private acquisition task.
 *
 * Serialised: only one instance runs at a time. start() is idempotent;
 * calling it again while already running is a no-op that returns 0.
 * -------------------------------------------------------------- */

/* Start the acquisition task. Returns 0 on success (running or newly
 * created), negative if the gsa slot is contested or thread creation
 * fails. @c initial_rollup is the today-so-far state to seed the accumulator's
 * "today rollup" with; pass NULL to start fresh at zero. */
int  health_worker_start(const health_daily_rollup_t *initial_rollup);

/* Create worker synchronization objects. Called once during module init. */
int  health_worker_init(void);

typedef enum
{
    HEALTH_STOP_FLUSH,
    HEALTH_STOP_DISCARD,
} health_stop_mode_t;

/* Request the worker to stop, either flushing or discarding pending data. */
void health_worker_stop(health_stop_mode_t mode);

/* True from successful start until the worker has completed cleanup. */
bool health_worker_is_running(void);

/* Force a flush of the current accumulator right now, independent of the
 * bucket deadline. Intended for the debug `health flush` shell command.
 * Returns true if a record was written (accumulator was non-empty),
 * false otherwise. Safe to call whether or not the worker is running. */
bool health_worker_flush_now(void);

/* Read the current today-rollup snapshot (thread-safe copy). Used by
 * app_health to publish EVT_HEALTH_STEPS_UPDATED and by the shell for
 * "current status" queries. */
void health_worker_get_today(health_daily_rollup_t *out);

/* Read the current UTC epoch seconds via /dev/rtc0. Returns 0 if the RTC
 * driver isn't available or the ioctl failed. Callers that need "today's
 * UTC day index" divide the result by 86400. Implemented in
 * health_worker.c because the worker already owns the RTC path helper. */
uint32_t health_worker_rtc_now_utc(void);

/* Called by health_worker.c immediately after a successful TSDB append —
 * hands the app-layer module the new today-rollup so it can (a) persist
 * to KV and (b) publish EVT_HEALTH_STEPS_UPDATED.
 *
 * Implemented in app_health.c. Runs on the WORKER task, not app_task —
 * the callee must not do anything blocking. */
void app_health_on_flush(const health_daily_rollup_t *rollup);

#ifdef __cplusplus
}
#endif

#endif /* __APP_HEALTH_INTERNAL_H__ */
