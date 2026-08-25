/*
 * health_worker.c
 *
 * Private acquisition task for the health module.
 *
 * Runs at HEALTH_WORKER_PRIO (2), below APP_TASK_PRIORITY (3) in OSIF,
 * so the 25 Hz sample loop never preempts the app_task event dispatcher.
 *
 * Data flow, per iteration:
 *
 *   posix_read(/dev/gsensor0)
 *     -> scale mg -> Q9 counts (1g ~ 512 in the 2g range)
 *     -> rtk_gsa_fsm(accs)
 *        -> pedometer_update_cb accumulates into BOTH s_bucket_acc (what gets
 *           persisted) and s_today_acc (what a watch face reads). Feeding the
 *           day total here rather than at flush time is why "today" tracks a
 *           walk in progress instead of lagging up to HEALTH_BUCKET_MIN.
 *
 * Persisting is driven from outside this task: EVT_TIME_TICK_15MIN arrives on
 * app_task, and health_worker_on_bucket_boundary() drains s_bucket_acc, builds
 * a health_pedo_record_t stamped with the boundary the tick reported, hands it
 * to health_db_append, snapshots today and notifies app_health — all on the
 * event dispatcher, so a sector erase there stalls event dispatch.
 *
 * Boundary ownership: this module no longer decides when a bucket ends.
 * app_time owns the wall clock and the timezone offset and publishes the
 * local :00/:15/:30/:45 boundaries, so every consumer agrees on which bucket
 * is current and two devices booted at different moments still align — which
 * is what makes cross-device aggregation on the phone possible. The local-day
 * rollover arrives the same way, as EVT_TIME_DAY_CHANGED.
 *
 * The worker is owned entirely by this module and uses the platform OSIF
 * task abstraction rather than an RTOS-specific API.
 */

#include "app_health_internal.h"
#include "app_time.h"
#include "app_log.h"

#include <errno.h>
#include <string.h>

#include <os_sched.h>
#include <os_sync.h>
#include <os_task.h>

#include "gsensor/rtk_gsa.h"
#if HEALTH_USE_SD001_SAMPLE
#include "sample_25hz_sd001.h"
#endif

#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_gsensor.h"

APP_LOG_MODULE_REGISTER(health_worker);

/* --------------------------------------------------------------
 * Accumulators, both fed directly by gsa's pedo_cb.
 *
 * Fine-grained units (cm, 0.01 kcal) rather than the record's coarse ones
 * (m, 0.1 kcal): rounding happens once, when a total is read out, instead of
 * on every callback. At ~1 callback/s that difference is the whole reason the
 * today total is kept here in raw form rather than as a health_daily_rollup_t.
 *
 *  - s_bucket_acc is drained by the flush routine every HEALTH_BUCKET_MIN.
 *  - s_today_acc spans the whole UTC day and is NOT touched by flush, so
 *    "today" reflects the walk in progress, not just what has been persisted.
 *
 * Both are updated in the same critical section as the sample that produced
 * them, so no reader can observe a step counted in neither.
 * -------------------------------------------------------------- */
typedef struct
{
    uint32_t steps;
    uint32_t distance_cm;   /* Q4 cm collapsed to cm (>>4) */
    uint32_t calories_x100; /* Q2 collapsed and scaled by 100 */
} health_acc_t;

static health_acc_t s_bucket_acc;
static uint8_t      s_bucket_last_mode;
static uint32_t     s_bucket_samples;

/* Zeroed by health_worker_on_day_changed(); app_time owns the notion of
 * "the local day rolled over", so no day index is tracked here. */
static health_acc_t s_today_acc;

static bool s_stop_requested;
static bool s_discard_requested;
static bool s_worker_running;
static void *s_worker_task_handle;
static void *s_state_mutex;
static void *s_flush_mutex;
#if HEALTH_USE_SD001_SAMPLE
static uint32_t s_sample_index;
#endif

static void mutex_take(void *mutex)
{
    (void)os_mutex_take(mutex, 0xFFFFFFFFu);
}

static void mutex_give(void *mutex)
{
    (void)os_mutex_give(mutex);
}

static bool stop_is_requested(void)
{
    uint32_t key = os_lock();
    bool requested = s_stop_requested;
    os_unlock(key);
    return requested;
}

static bool discard_is_requested(void)
{
    uint32_t key = os_lock();
    bool requested = s_discard_requested;
    os_unlock(key);
    return requested;
}

/* Runs on app_task. A couple of stores under the state lock. */
void health_worker_on_day_changed(void)
{
    mutex_take(s_state_mutex);
    memset(&s_today_acc, 0, sizeof(s_today_acc));
    mutex_give(s_state_mutex);
    APP_LOGI("local day changed: today totals reset");
}

static void pedometer_update_cb(gsa_pedo_info_t *info)
{
    uint32_t dist_cm  = (uint32_t)info->distance >> 4;
    uint32_t cal_x100 = ((uint32_t)info->calories * 100u) >> 2;

    mutex_take(s_state_mutex);

    /* Feed both totals from the same sample, under one lock, so no reader can
     * see a step counted in neither. The bucket is what gets persisted; the day
     * total is what a watch face shows, and it must not wait for the next
     * flush to move.
     *
     * No day-rollover check here: this runs inside the 25 Hz sample path and
     * reading the RTC costs three posix calls. Rollover is detected when a
     * total is read or flushed instead, which is where it was always handled. */
    s_bucket_acc.steps         += info->steps;
    s_bucket_acc.distance_cm   += dist_cm;
    s_bucket_acc.calories_x100 += cal_x100;
    s_bucket_last_mode          = (uint8_t)info->mode;
    s_bucket_samples++;

    s_today_acc.steps         += info->steps;
    s_today_acc.distance_cm   += dist_cm;
    s_today_acc.calories_x100 += cal_x100;

    uint32_t bucket_steps = s_bucket_acc.steps;
    uint32_t today_steps  = s_today_acc.steps;
    mutex_give(s_state_mutex);

    APP_LOGI("pedo update: +%u steps mode=%u dist=%ucm bucket=%u today=%u",
             (unsigned)info->steps, (unsigned)info->mode,
             (unsigned)dist_cm, (unsigned)bucket_steps, (unsigned)today_steps);
}

static bool gsa_algorithm_init(uint32_t odr_hz)
{
    /* rtk_gsa_init takes pointers that MUST outlive the SDK's use of them;
     * keep them file-static so the memory is trivially long-lived. */
    static usr_prof_t prof =
    {
        .gender = 1, .age = 30,
        .height = 170.0f, .weight = 65.0f,
    };
    static gsa_gs_inf_t gs =
    {
        .xpos = GS_XPOS_RIGHT,
        .zpos = GS_ZPOS_UP,
    };
    static gsa_cbs_t cbs =
    {
        .pedo_cb = pedometer_update_cb,
    };
    gs.odr = (float)odr_hz;
    return rtk_gsa_init(&prof, &gs, &cbs);
}

/* Put a failed flush's snapshot back into the bucket, folded in with whatever
 * the sample loop collected while the flush was in flight, so a rejected write
 * costs no steps. The today total is untouched here — it never lost the
 * samples in the first place, since flush does not drain it. */
static void bucket_merge_back(const health_acc_t *snap, uint8_t mode)
{
    mutex_take(s_state_mutex);
    s_bucket_acc.steps         += snap->steps;
    s_bucket_acc.distance_cm   += snap->distance_cm;
    s_bucket_acc.calories_x100 += snap->calories_x100;
    if (s_bucket_samples == 0u)
    {
        s_bucket_last_mode = mode;
    }
    mutex_give(s_state_mutex);
}

/* Render the today accumulator into the record-facing units the rollup uses.
 * Rounding happens here, once per read, rather than on every callback.
 * Caller must hold s_state_mutex. */
static void today_snapshot(health_daily_rollup_t *out)
{
    out->steps          = s_today_acc.steps;
    out->distance_m     = s_today_acc.distance_cm / 100u;
    out->calories_dkcal = s_today_acc.calories_x100 / 10000u;
}

/* --------------------------------------------------------------
 * Flush path.
 *
 * Two callers: the bucket-boundary tick (on app_task, via
 * health_worker_on_bucket_boundary) and the worker's own stop path.
 *
 * @c boundary_sec is the record's timestamp. For a scheduled flush it is the
 * boundary app_time reported, NOT the moment this runs — the tick travels
 * through the event queue, so reading the clock here would drift the record
 * off the boundary. A partial flush passes 0 and gets the current time
 * instead, since there is no boundary to align to.
 *
 * Drains the bucket only — the today total is fed directly by the sample
 * callback and is never drained here, so a flush failure cannot make today's
 * step count go backwards.
 *
 * Empty buckets are skipped so a static device doesn't burn flash on
 * zero-value records. A rejected write puts the snapshot back into the bucket,
 * so no failure path loses steps.
 * -------------------------------------------------------------- */
static void flush_step_bucket(bool partial_bucket, uint32_t boundary_sec)
{
    mutex_take(s_flush_mutex);

    /* Snapshot and clear the current bucket before writing it. */
    health_acc_t snap;
    uint8_t      mode;
    mutex_take(s_state_mutex);
    snap = s_bucket_acc;
    mode = s_bucket_last_mode;
    memset(&s_bucket_acc, 0, sizeof(s_bucket_acc));
    s_bucket_samples = 0u;
    mutex_give(s_state_mutex);

    if (snap.steps == 0 && snap.distance_cm == 0 && snap.calories_x100 == 0)
    {
        mutex_give(s_flush_mutex);
        return;
    }

    /* Clip to record field widths. Overflow is impossible in practice for a
     * 15-minute bucket (200 steps/min * 15 = 3000 << 0xFFFF) but the clip
     * keeps the invariant explicit for future longer buckets. */
    uint32_t steps_clip = (snap.steps > 0xFFFF) ? 0xFFFF : snap.steps;
    uint32_t dist_m     = snap.distance_cm / 100u;
    if (dist_m > 0xFFFF) { dist_m = 0xFFFF; }
    /* calories_x100 units are 0.01 cal; 0.1 kcal is 100 cal = 10000 units. */
    uint32_t calories_dkcal = snap.calories_x100 / 10000u;
    if (calories_dkcal > 0xFFFF) { calories_dkcal = 0xFFFF; }

    /* A scheduled flush is stamped with the boundary the tick reported; the
     * stop path has no boundary to align to, so it asks app_time — the clock
     * owner — for the current instant. */
    uint32_t ts = (boundary_sec != 0u) ? boundary_sec : app_time_now();
    if (ts == 0)
    {
        APP_LOGE("flush rejected: RTC time is invalid");
        bucket_merge_back(&snap, mode);
        mutex_give(s_flush_mutex);
        return;
    }

    health_pedo_record_t rec =
    {
        .ts     = ts,
        .steps      = (uint16_t)steps_clip,
        .distance_m = (uint16_t)dist_m,
        .calories_dkcal = (uint16_t)calories_dkcal,
        .hr_avg     = 0,
        .bucket_min = (uint8_t)HEALTH_BUCKET_MIN,
        .mode       = mode,
        .flags      = partial_bucket ? HEALTH_RECORD_FLAG_PARTIAL_BUCKET : 0u,
    };

    int append_rc = health_db_append_pedo(&rec);
    if (append_rc != 0)
    {
        APP_LOGE("flush failed rc=%d; data retained in accumulator", append_rc);
        bucket_merge_back(&snap, mode);
        mutex_give(s_flush_mutex);
        return;
    }

    /* The today total already includes this bucket — it was accumulated as the
     * samples arrived — so just snapshot it for the event. */
    health_daily_rollup_t today;
    mutex_take(s_state_mutex);
    today_snapshot(&today);
    mutex_give(s_state_mutex);

    APP_LOGI("flushed: steps=%u dist=%um cal=%u.%u kcal (today: %u steps)",
             (unsigned)steps_clip, (unsigned)dist_m,
             (unsigned)(calories_dkcal / 10u),
             (unsigned)(calories_dkcal % 10u),
             (unsigned)today.steps);

    /* Hand up to app_health for KV persistence + event publish. Runs on
     * the worker task; app_health_on_flush must not block. */
    app_health_on_flush(&today);
    mutex_give(s_flush_mutex);
}

/* Bucket boundary reached — write the closed bucket to the TSDB now.
 *
 * Runs on app_task, i.e. the flash write happens on the event dispatcher.
 * That is deliberate: the caller sees the record land as a direct consequence
 * of the tick, with no latch or hand-off in between. The cost is that a TSDB
 * append which happens to trigger a 4 KB sector erase stalls event dispatch
 * for the duration of the erase.
 *
 * A no-op when no worker is running: there is no bucket being filled, and
 * flushing would write a record for a period nothing was sampled. */
void health_worker_on_bucket_boundary(uint32_t boundary_sec)
{
    if (boundary_sec == 0u) { return; }

    if (!health_worker_is_running())
    {
        return;
    }

    flush_step_bucket(false, boundary_sec);
}

/* Throw away everything accumulated for today. Used by the explicit-unbind
 * path so a later bind by a different user cannot inherit a stranger's step
 * count. Reached both from the worker's own exit path and from
 * health_worker_stop() when no worker is running. */
static void discard_today(void)
{
    mutex_take(s_flush_mutex);
    mutex_take(s_state_mutex);
    memset(&s_bucket_acc, 0, sizeof(s_bucket_acc));
    s_bucket_last_mode = 0u;
    s_bucket_samples   = 0u;
    memset(&s_today_acc, 0, sizeof(s_today_acc));
    mutex_give(s_state_mutex);
    mutex_give(s_flush_mutex);
}

/* --------------------------------------------------------------
 * Task body.
 * -------------------------------------------------------------- */

static void worker_fn(void *context)
{
    (void)context;

    /* posix_port_init_all is idempotent internally in every example we've
     * seen, but the health path may be the first caller; leave the guard
     * here so we don't depend on that. */
    static bool s_posix_inited = false;
    if (!s_posix_inited) { posix_port_init_all(); s_posix_inited = true; }

    posix_fd_t gs = posix_open("/dev/gsensor0");
    if (gs == POSIX_FD_NULL)
    {
        APP_LOGE("open /dev/gsensor0 failed");
        goto done;
    }

    posix_gsensor_config_t cfg =
    {
        .range     = POSIX_GSENSOR_RANGE_2G,
        .odr_hz    = HEALTH_ODR_HZ,
        .low_power = 0,
        .use_irq   = 0,
    };
    if (posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg) != 0)
    {
        APP_LOGE("gsensor SET_CONFIG failed");
        posix_close(gs);
        goto done;
    }
    if (!gsa_algorithm_init(HEALTH_ODR_HZ))
    {
        APP_LOGE("rtk_gsa_init failed");
        posix_close(gs);
        goto done;
    }
    /* Start the bucket clean; the today total is seeded by start() and must
     * survive a worker restart within the same day. */
    mutex_take(s_state_mutex);
    memset(&s_bucket_acc, 0, sizeof(s_bucket_acc));
    s_bucket_last_mode = 0u;
    s_bucket_samples   = 0u;
    mutex_give(s_state_mutex);
#if HEALTH_USE_SD001_SAMPLE
    s_sample_index = 0u;
    APP_LOGW("using embedded SD_001 sample as pedometer input");
#endif

    const uint32_t period_ms = 1000u / HEALTH_ODR_HZ;

    /* The loop only samples; persisting a bucket is driven from outside, by
     * EVT_TIME_TICK_15MIN landing in health_worker_on_bucket_boundary(). */
    APP_LOGI("worker started: %uHz, bucket=%umin (flush driven by "
             "EVT_TIME_TICK_15MIN)",
             (unsigned)HEALTH_ODR_HZ, (unsigned)HEALTH_BUCKET_MIN);

    while (!stop_is_requested())
    {
        posix_gsensor_axis_t axis = {0};
        if (posix_read(gs, &axis, sizeof(axis)) == (posix_ssize_t)sizeof(axis))
        {
#if HEALTH_USE_SD001_SAMPLE
            int16_t accs[3] =
            {
                gsa_sample_25hz_sd001[s_sample_index][0],
                gsa_sample_25hz_sd001[s_sample_index][1],
                gsa_sample_25hz_sd001[s_sample_index][2],
            };
            s_sample_index++;
            if (s_sample_index >= GSA_SAMPLE_25HZ_SD001_N)
            {
                s_sample_index = 0u;
                APP_LOGI("SD_001 sample restarted");
            }
#else
            int16_t accs[3] =
            {
                (int16_t)((axis.x * 512) / 1000),
                (int16_t)((axis.y * 512) / 1000),
                (int16_t)((axis.z * 512) / 1000),
            };
#endif
            rtk_gsa_fsm(accs);
        }

        os_delay(period_ms);
    }

    if (discard_is_requested())
    {
        discard_today();
    }
    else
    {
        /* Stop path: one last flush so partial data isn't silently lost. */
        flush_step_bucket(true, 0u);
    }
    posix_close(gs);
    APP_LOGI("worker stopped");

done:
    void *task_handle = s_worker_task_handle;
    s_worker_task_handle = NULL;
    uint32_t key = os_lock();
    s_stop_requested = false;
    s_discard_requested = false;
    s_worker_running = false;
    os_unlock(key);
    (void)os_task_delete(task_handle);
}

/* --------------------------------------------------------------
 * Public API.
 * -------------------------------------------------------------- */

int health_worker_init(void)
{
    if (s_state_mutex == NULL && !os_mutex_create(&s_state_mutex))
    {
        return -ENOMEM;
    }
    if (s_flush_mutex == NULL && !os_mutex_create(&s_flush_mutex))
    {
        return -ENOMEM;
    }
    return 0;
}

int health_worker_start(void)
{
    uint32_t key = os_lock();
    if (s_worker_running)
    {
        os_unlock(key);
        APP_LOGW("health worker already running");
        return -EBUSY;
    }
    s_worker_running = true;
    os_unlock(key);

    /* Today's totals are RAM-only and start at zero: nothing is restored from
     * flash, so a reboot mid-day loses the running total by design. The
     * per-bucket records already in the TSDB remain the durable history. */
    mutex_take(s_state_mutex);
    memset(&s_today_acc, 0, sizeof(s_today_acc));
    mutex_give(s_state_mutex);

    key = os_lock();
    s_stop_requested = false;
    s_discard_requested = false;
    os_unlock(key);

    if (!os_task_create(&s_worker_task_handle, "health", worker_fn, NULL,
                        HEALTH_WORKER_STACK, HEALTH_WORKER_PRIO))
    {
        key = os_lock();
        s_worker_running = false;
        os_unlock(key);
        APP_LOGE("health worker task creation failed");
        return -ENOMEM;
    }
    return 0;
}

void health_worker_stop(health_stop_mode_t mode)
{
    bool discard = (mode == HEALTH_STOP_DISCARD);
    uint32_t key = os_lock();
    bool running = s_worker_running;
    if (running)
    {
        if (discard) { s_discard_requested = true; }
        s_stop_requested = true;
    }
    os_unlock(key);

    /* A running worker discards on its way out; if none is running there is
     * nobody to do it, so clear the state here. */
    if (!running && discard)
    {
        discard_today();
    }
}

bool health_worker_is_running(void)
{
    uint32_t key = os_lock();
    bool running = s_worker_running;
    os_unlock(key);
    return running;
}


/* Today's totals, current as of the last sample — the accumulator behind this
 * is fed by pedometer_update_cb, not by flush, so a walk in progress is
 * already reflected here. */
void health_worker_get_today(health_daily_rollup_t *out)
{
    if (out == NULL) { return; }
    mutex_take(s_state_mutex);
    today_snapshot(out);
    mutex_give(s_state_mutex);
}
