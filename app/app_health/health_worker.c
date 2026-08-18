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
 *        -> pedometer_update_cb accumulates into s_bucket_acc
 *   every HEALTH_BUCKET_SEC of wall-clock (aligned to UTC bucket
 *   boundaries): drain s_bucket_acc, build a health_pedo_record_t, hand it to
 *   health_db_append, update today-rollup, notify app_health.
 *
 * Boundary alignment note: bucket edges are computed against UTC epoch
 * seconds (via /dev/rtc0), not against OS uptime. Two devices
 * booting at different moments still flush at the same wall-clock
 * minutes (:00, :15, :30, :45), which matters for cross-device
 * aggregation on the phone side later.
 *
 * The worker is owned entirely by this module and uses the platform OSIF
 * task abstraction rather than an RTOS-specific API.
 */

#include "app_health_internal.h"
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
#include "ioctls/posix_ioctl_rtc.h"

APP_LOG_MODULE_REGISTER(health_worker);

/* --------------------------------------------------------------
 * Accumulator: fed by gsa's pedo_cb, drained by the flush routine.
 * Kept in Q-scaled integers on the way in to defer rounding until the
 * flush so sub-unit precision is retained across callbacks.
 * -------------------------------------------------------------- */
static struct
{
    uint32_t steps;
    uint32_t distance_cm;  /* Q4 cm collapsed to cm (>>4); flush divides to metres */
    uint32_t calories_x100; /* Q2 cal scaled by 100 to keep precision until flush  */
    uint8_t  last_mode;
    uint32_t sample_count;
} s_bucket_acc;

/* Today rollup. Written on flush; a copy is exposed via
 * health_worker_get_today() for consumers (app_health, shell). */
static health_daily_rollup_t     s_today_rollup;

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

static void pedometer_update_cb(gsa_pedo_info_t *info)
{
    mutex_take(s_state_mutex);
    s_bucket_acc.steps         += info->steps;
    s_bucket_acc.distance_cm   += (uint32_t)info->distance >> 4;
    s_bucket_acc.calories_x100 += ((uint32_t)info->calories * 100u) >> 2;
    s_bucket_acc.last_mode      = (uint8_t)info->mode;
    s_bucket_acc.sample_count++;
    uint32_t bucket_steps = s_bucket_acc.steps;
    mutex_give(s_state_mutex);

    APP_LOGI("pedo update: +%u steps mode=%u dist=%ucm bucket=%u",
             (unsigned)info->steps, (unsigned)info->mode,
             (unsigned)((uint32_t)info->distance >> 4),
             (unsigned)bucket_steps);
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

/* --------------------------------------------------------------
 * Wall-clock helpers.
 * -------------------------------------------------------------- */

static uint32_t rtc_time_to_epoch(const posix_rtc_time_t *time)
{
    if (time->year < 1970 || time->month < 1 || time->month > 12 ||
        time->mday < 1 || time->mday > 31 || time->hour > 23 ||
        time->minute > 59 || time->second > 60)
    {
        return 0;
    }

    int32_t year = (int32_t)time->year - (time->month <= 2 ? 1 : 0);
    int32_t era = year / 400;
    uint32_t year_of_era = (uint32_t)(year - era * 400);
    uint32_t month = time->month + (time->month > 2 ? -3u : 9u);
    uint32_t day_of_year = (153u * month + 2u) / 5u + time->mday - 1u;
    uint32_t day_of_era = year_of_era * 365u + year_of_era / 4u -
                          year_of_era / 100u + day_of_year;
    int64_t days = (int64_t)era * 146097 + (int64_t)day_of_era - 719468;

    return (uint32_t)(days * 86400 +
                      (int64_t)time->hour * 3600 +
                      (int64_t)time->minute * 60 +
                      (int64_t)time->second);
}

/* Current UTC epoch seconds, or 0 if the RTC is unavailable. Public because
 * app_health needs "today's UTC day" when it seeds the rollup at arming time,
 * and the RTC path already lives here. */
uint32_t health_worker_rtc_now_utc(void)
{
    posix_fd_t rtc = posix_open(HEALTH_RTC_PATH);
    if (rtc == POSIX_FD_NULL) { return 0; }

    posix_rtc_time_t t;
    int rc = posix_ioctl(rtc, POSIX_RTC_IOCTL_GET_TIME, &t);
    posix_close(rtc);
    if (rc != POSIX_OK) { return 0; }

    return rtc_time_to_epoch(&t);
}

/* Delay from now until the next absolute UTC bucket boundary. */
static uint32_t next_bucket_delay_ms(uint32_t now_utc)
{
    if (now_utc == 0)
    {
        return HEALTH_BUCKET_SEC * 1000u;
    }
    uint32_t elapsed = now_utc % HEALTH_BUCKET_SEC;
    return (HEALTH_BUCKET_SEC - elapsed) * 1000u;
}

/* Zero the rollup if @c utc_day_index names a different day than the cached
 * one. Detecting the rollover on access is why no midnight timer is needed.
 * Caller must hold s_state_mutex. */
static void today_roll_to_day(uint32_t utc_day_index)
{
    if (utc_day_index != s_today_rollup.utc_day_index)
    {
        memset(&s_today_rollup, 0, sizeof(s_today_rollup));
        s_today_rollup.utc_day_index = utc_day_index;
    }
}

/* Put a failed flush's snapshot back into the accumulator, folded in with
 * whatever the sample loop collected while the flush was in flight, so a
 * rejected write costs no steps. */
static void acc_merge_back(uint32_t steps, uint32_t dist_cm,
                           uint32_t calories_x100, uint8_t mode)
{
    mutex_take(s_state_mutex);
    s_bucket_acc.steps += steps;
    s_bucket_acc.distance_cm += dist_cm;
    s_bucket_acc.calories_x100 += calories_x100;
    if (s_bucket_acc.sample_count == 0u)
    {
        s_bucket_acc.last_mode = mode;
    }
    mutex_give(s_state_mutex);
}

/* --------------------------------------------------------------
 * Flush path.
 *
 * Two callers, both in the worker loop: the bucket boundary, and one final
 * time on stop.
 *
 * Empty buckets are skipped so a static device doesn't burn flash on
 * zero-value records. A rejected write puts the snapshot back into the
 * accumulator, so no failure path loses steps.
 * -------------------------------------------------------------- */
static void flush_step_bucket(bool partial_bucket)
{
    mutex_take(s_flush_mutex);

    /* Snapshot and clear the current bucket before writing it. */
    uint32_t steps, dist_cm, calories_x100;
    uint8_t  mode;
    mutex_take(s_state_mutex);
    steps    = s_bucket_acc.steps;
    dist_cm  = s_bucket_acc.distance_cm;
    calories_x100 = s_bucket_acc.calories_x100;
    mode     = s_bucket_acc.last_mode;
    memset(&s_bucket_acc, 0, sizeof(s_bucket_acc));
    mutex_give(s_state_mutex);

    if (steps == 0 && dist_cm == 0 && calories_x100 == 0)
    {
        mutex_give(s_flush_mutex);
        return;
    }

    /* Clip to record field widths. Overflow is impossible in practice for a
     * 15-minute bucket (200 steps/min * 15 = 3000 << 0xFFFF) but the clip
     * keeps the invariant explicit for future longer buckets. */
    uint32_t steps_clip = (steps > 0xFFFF) ? 0xFFFF : steps;
    uint32_t dist_m     = dist_cm / 100u;
    if (dist_m > 0xFFFF) { dist_m = 0xFFFF; }
    /* calories_x100 units are 0.01 cal; 0.1 kcal is 100 cal = 10000 units. */
    uint32_t calories_dkcal = calories_x100 / 10000u;
    if (calories_dkcal > 0xFFFF) { calories_dkcal = 0xFFFF; }

    uint32_t ts = health_worker_rtc_now_utc();
    if (ts == 0)
    {
        APP_LOGE("flush rejected: RTC time is invalid");
        acc_merge_back(steps, dist_cm, calories_x100, mode);
        mutex_give(s_flush_mutex);
        return;
    }

    health_pedo_record_t rec =
    {
        .ts_utc     = ts,
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
        acc_merge_back(steps, dist_cm, calories_x100, mode);
        mutex_give(s_flush_mutex);
        return;
    }

    /* Update today rollup. The day rollover is handled inside
     * today_roll_to_day, so no separate midnight timer is needed. */
    health_daily_rollup_t today_rollup_snapshot;
    mutex_take(s_state_mutex);
    today_roll_to_day(rec.ts_utc / 86400u);
    s_today_rollup.steps      += steps_clip;
    s_today_rollup.distance_m += dist_m;
    s_today_rollup.calories_dkcal += calories_dkcal;
    today_rollup_snapshot = s_today_rollup;
    mutex_give(s_state_mutex);

    APP_LOGI("flushed: steps=%u dist=%um cal=%u.%u kcal (today: %u steps)",
             (unsigned)steps_clip, (unsigned)dist_m,
             (unsigned)(calories_dkcal / 10u),
             (unsigned)(calories_dkcal % 10u),
             (unsigned)today_rollup_snapshot.steps);

    /* Hand up to app_health for KV persistence + event publish. Runs on
     * the worker task; app_health_on_flush must not block. */
    app_health_on_flush(&today_rollup_snapshot);
    mutex_give(s_flush_mutex);
}

/* Throw away everything accumulated for today, on disk and in RAM. Used by
 * the explicit-unbind path so a later bind by a different user cannot inherit
 * a stranger's step count. Reached both from the worker's own exit path and
 * from health_worker_stop() when no worker is running. */
static void discard_today(void)
{
    mutex_take(s_flush_mutex);
    mutex_take(s_state_mutex);
    memset(&s_bucket_acc, 0, sizeof(s_bucket_acc));
    memset(&s_today_rollup, 0, sizeof(s_today_rollup));
    mutex_give(s_state_mutex);
    health_daily_rollup_t cleared = {0};
    (void)health_db_save_today(&cleared);
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

    posix_fd_t gs = posix_open(HEALTH_GS_PATH);
    if (gs == POSIX_FD_NULL)
    {
        APP_LOGE("open %s failed", HEALTH_GS_PATH);
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
    mutex_take(s_state_mutex);
    memset(&s_bucket_acc, 0, sizeof(s_bucket_acc));
    mutex_give(s_state_mutex);
#if HEALTH_USE_SD001_SAMPLE
    s_sample_index = 0u;
    APP_LOGW("using embedded SD_001 sample as pedometer input");
#endif

    const uint32_t period_ms = 1000u / HEALTH_ODR_HZ;

    uint32_t next_flush_rel =
        (uint32_t)os_sys_time_get() + next_bucket_delay_ms(health_worker_rtc_now_utc());

    APP_LOGI("worker started: %uHz, bucket=%umin",
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

        bool due = ((int32_t)((uint32_t)os_sys_time_get() - next_flush_rel) >= 0);

        if (due)
        {
            flush_step_bucket(false);
            next_flush_rel =
                (uint32_t)os_sys_time_get() + next_bucket_delay_ms(health_worker_rtc_now_utc());
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
        flush_step_bucket(true);
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

int health_worker_start(const health_daily_rollup_t *initial_rollup)
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

    mutex_take(s_state_mutex);
    if (initial_rollup != NULL) { s_today_rollup = *initial_rollup; }
    else                        { memset(&s_today_rollup, 0, sizeof(s_today_rollup)); }
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


void health_worker_get_today(health_daily_rollup_t *out)
{
    if (out == NULL) { return; }
    uint32_t now = health_worker_rtc_now_utc();
    mutex_take(s_state_mutex);
    if (now != 0u)
    {
        today_roll_to_day(now / 86400u);
    }
    *out = s_today_rollup;
    mutex_give(s_state_mutex);
}
