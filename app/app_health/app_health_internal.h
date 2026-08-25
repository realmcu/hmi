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
 *  - health_db.c     : owns the pedo TSDB + the sync watermark KV; provides
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

#define HEALTH_ODR_HZ            25u

/* Debug input source: keep the sensor read as the 25 Hz pacing source, but
 * feed the embedded SD_001 Q9 trace into GSA instead of live sensor data. */
#define HEALTH_USE_SD001_SAMPLE  0

/* OSIF uses larger numbers for higher priorities. Keep the worker below
 * app_task (priority 3) so sampling cannot preempt event dispatch. */
#define HEALTH_WORKER_PRIO       2
#define HEALTH_WORKER_STACK      2048u


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
    uint32_t ts;            /* wall clock seconds at flush moment           */
    uint16_t steps;         /* clipped to 0xFFFF                            */
    uint16_t distance_m;    /* metres, clipped to 0xFFFF                    */
    uint16_t calories_dkcal; /* 0.1 kcal units, clipped to 0xFFFF           */
    uint8_t  hr_avg;        /* bpm — 0 when hr not implemented yet          */
    uint8_t  bucket_min;    /* always HEALTH_BUCKET_MIN today, kept for     */
    /*  forward-compat if product ever changes it   */
    uint8_t  mode;          /* 0=walk 1=run 2=invalid (gsa_pedo_info.mode)  */
    uint8_t  flags;         /* HEALTH_RECORD_FLAG_*                         */
    uint32_t reserved;      /* pad to 18 bytes; future field growth room    */
} __attribute__((packed)) health_pedo_record_t;

#define HEALTH_RECORD_FLAG_HAS_HR          (1u << 0)
#define HEALTH_RECORD_FLAG_PARTIAL_BUCKET  (1u << 1)

_Static_assert(sizeof(health_pedo_record_t) == 18,
               "health_pedo_record_t must stay 18 bytes; flash layout depends on it");

/* --------------------------------------------------------------
 * Aggregated view of "today so far", rendered on demand from
 * health_worker's RAM accumulator. Never persisted: a reboot restarts the
 * day at zero, and the per-bucket TSDB records are the durable history.
 * The accumulator behind it is zeroed when the UTC day rolls over, which is
 * detected on read rather than by a midnight timer.
 * -------------------------------------------------------------- */
typedef struct
{
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

/* Append one 18B pedometer record to the TSDB, using the record's ts as
 * the time key. An equal-to-last timestamp is advanced by one second and
 * reflected back into rec; a clock rollback is rejected with -ERANGE.
 * Returns 0 on success, negative on failure. */
int  health_db_append_pedo(health_pedo_record_t *rec);

/* The sequential read cursor is NOT declared here: health_db.c defines
 * app_health_history_read() directly, since the batch cursor and the persisted
 * watermark it advances are both file-static there. See app_health.h. */

/* --------------------------------------------------------------
 * health_worker — private acquisition task.
 *
 * Serialised: only one instance runs at a time. start() is idempotent;
 * calling it again while already running is a no-op that returns 0.
 * -------------------------------------------------------------- */

/* Start the acquisition task. Today's totals begin at zero — they are RAM-only
 * and nothing is restored from flash. Returns 0 on success (running or newly
 * created), negative if the gsa slot is contested or thread creation fails. */
int  health_worker_start(void);

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

/* Read the current today-rollup snapshot (thread-safe copy). Used by
 * app_health to publish EVT_HEALTH_STEPS_UPDATED and to back
 * app_health_get_today(). */
void health_worker_get_today(health_daily_rollup_t *out);

/* Bucket boundary reached: write the closed bucket to the TSDB. @c boundary_sec
 * is the boundary instant in wall clock seconds and becomes the record's
 * timestamp. A no-op when no worker is running. */
void health_worker_on_bucket_boundary(uint32_t boundary_sec);

/* Local day rolled over: zero today's running totals. */
void health_worker_on_day_changed(void);

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
