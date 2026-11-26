/*
 * VC30x heart-rate / SpO2 acquisition service.
 *
 * The POSIX port owns the register-level sensor protocol. This file owns the
 * product policy: battery-conscious spot measurements, the Vcare PPG->BPM
 * algorithm, pulse/off-skin gating, and the app-layer state exposed to UI.
 *
 * SpO2 remains raw red/IR in this firmware. The referenced project does not
 * contain a validated percentage algorithm, so this module deliberately does
 * not fabricate a medical value from an uncalibrated ratio.
 */

#include "health_hrm.h"

#include <errno.h>
#include <string.h>

#include <os_sched.h>
#include <os_sync.h>
#include <os_task.h>
#include <zephyr/kernel.h>

#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"
#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_hrm.h"
#include "vc_hr_algo.h"

APP_LOG_MODULE_REGISTER(health_hrm);

#define HRM_DEVICE_PATH          "/dev/hrm0"
#define HRM_TASK_STACK           2048u
#define HRM_TASK_PRIO            2u

#define HRM_FIRST_SPOT_MS        5000u
#define HRM_SPOT_PERIOD_MS       (5u * 60u * 1000u)
#define HRM_SPOT_WINDOW_MS       60000u
#define HRM_SPOT_SETTLE_MS       15000u
#define HRM_FIRST_SPO2_MS        10000u

#define HRM_GAP_MS               (1000u / 25u)
#define HRM_SURFACE_RECOG        1
#define HRM_OPTICAL_AID          1

#define HRM_P2P_MIN              30u
#define HRM_P2P_WINDOW           5u
#define HRM_P2P_VOTES            3u

typedef struct
{
    uint32_t started_ms;
    uint32_t first_hr_ms;
    uint16_t last_p2p;
    uint8_t pulse_hist;
    uint8_t pulse_frames;
    bool pulse;
    bool worn;
    uint8_t polls;
} hrm_session_t;

static posix_fd_t s_fd = POSIX_FD_NULL;
static void *s_task;
static volatile bool s_suspended;
static volatile bool s_hr_requested;
static volatile bool s_stop_hr;
static volatile bool s_spo2_requested;
static volatile bool s_stop_spo2;
static app_health_hrm_state_t s_state;
static uint32_t s_next_hr_ms;
static uint32_t s_next_spo2_ms;

static bool time_reached(uint32_t now, uint32_t due)
{
    return (int32_t)(now - due) >= 0;
}

static void state_set_error(int error)
{
    uint32_t key = os_lock();
    s_state.last_error = error;
    if (error != 0)
    {
        s_state.hr_valid = false;
        s_state.spo2_raw_valid = false;
    }
    os_unlock(key);
}

static void state_set_present(bool present)
{
    uint32_t key = os_lock();
    s_state.sensor_present = present;
    os_unlock(key);
}

static void state_set_hr_measuring(bool measuring)
{
    uint32_t key = os_lock();
    s_state.hr_measuring = measuring;
    os_unlock(key);
}

static void state_set_spo2_measuring(bool measuring)
{
    uint32_t key = os_lock();
    s_state.spo2_measuring = measuring;
    os_unlock(key);
}

static void publish_hr(uint8_t bpm, bool valid)
{
    uint32_t key = os_lock();
    bool changed = s_state.hr_valid != valid || (valid && s_state.bpm != bpm);
    s_state.hr_valid = valid;
    s_state.bpm = valid ? bpm : 0u;
    os_unlock(key);

    if (changed && valid)
    {
        (void)app_event_publish(EVT_HEALTH_HR_UPDATED, &bpm, sizeof(bpm));
    }
}

static void publish_spo2_raw(uint16_t red, uint16_t ir)
{
    uint32_t key = os_lock();
    s_state.spo2_red = red;
    s_state.spo2_ir = ir;
    s_state.spo2_raw_valid = (red != 0u || ir != 0u);
    s_state.last_error = 0;
    os_unlock(key);
}

static bool get_ioctl_state(posix_hrm_status_t *status)
{
    if (s_fd == POSIX_FD_NULL || status == NULL)
    {
        return false;
    }
    return posix_ioctl(s_fd, POSIX_HRM_IOCTL_GET_STATUS, status) == POSIX_OK;
}

static bool probe_sensor(void)
{
    posix_hrm_status_t status;
    if (get_ioctl_state(&status) && status.link_ok)
    {
        state_set_present(true);
        return true;
    }

    (void)posix_ioctl(s_fd, POSIX_HRM_IOCTL_SELF_TEST, NULL);
    if (!get_ioctl_state(&status) || !status.link_ok)
    {
        state_set_present(false);
        state_set_error(POSIX_ERR_IO);
        return false;
    }

    state_set_present(true);
    state_set_error(0);
    APP_LOGI("VC30x present: chip=0x%02x version=0x%02x hr=%d spo2=%d",
             status.chip_id, status.version, (int)status.hr_ok, (int)status.spo2_ok);
    return true;
}

static uint16_t frame_p2p(const posix_hrm_frame_t *frame)
{
    uint8_t first = 0;
    for (uint8_t i = 0; i < frame->count; i++)
    {
        if (frame->samples[i] & 0x1000u)
        {
            first = (uint8_t)(i + 1u);
        }
    }
    if ((uint8_t)(frame->count - first) < 10u)
    {
        return 0;
    }

    uint16_t lo = 0x0FFFu;
    uint16_t hi = 0u;
    for (uint8_t i = first; i < frame->count; i++)
    {
        const uint16_t value = frame->samples[i] & 0x0FFFu;
        lo = value < lo ? value : lo;
        hi = value > hi ? value : hi;
    }
    return (uint16_t)(hi - lo);
}

static bool pulse_vote(hrm_session_t *session, uint16_t p2p)
{
    const uint8_t mask = (uint8_t)((1u << HRM_P2P_WINDOW) - 1u);
    session->pulse_hist = (uint8_t)(((session->pulse_hist << 1) |
                                     (p2p >= HRM_P2P_MIN ? 1u : 0u)) & mask);
    if (session->pulse_frames < HRM_P2P_WINDOW)
    {
        session->pulse_frames++;
    }
    return session->pulse_frames >= HRM_P2P_WINDOW &&
           (uint8_t)__builtin_popcount(session->pulse_hist) >= HRM_P2P_VOTES;
}

static void run_algorithm(const posix_hrm_frame_t *frame, AlgoOutputData_t *out)
{
    AlgoInputData_t input = {0};
    input.envSample = frame->env;
    for (uint8_t i = 0; i < frame->count; i++)
    {
        input.ppgSample = (int32_t)(frame->samples[i] & 0x0FFFu);
        Algo_Input(&input, HRM_GAP_MS, SPORT_TYPE_PERIODIC_MONITORING,
                   HRM_SURFACE_RECOG, HRM_OPTICAL_AID);
    }
    memset(out, 0, sizeof(*out));
    Algo_Output(out);
}

static int open_mode(posix_hrm_mode_t mode)
{
    return posix_ioctl(s_fd, POSIX_HRM_IOCTL_OPEN, &mode) == POSIX_OK ? 0 : -EIO;
}

static void close_sensor(void)
{
    if (s_fd != POSIX_FD_NULL)
    {
        (void)posix_ioctl(s_fd, POSIX_HRM_IOCTL_STOP, NULL);
    }
}

static void hr_spot(void)
{
    if (open_mode(POSIX_HRM_MODE_HR) != 0)
    {
        state_set_error(-EIO);
        APP_LOGW("HR open failed");
        return;
    }

    hrm_session_t session = {0};
    session.started_ms = k_uptime_get_32();
    session.worn = true;
    Algo_Init();
    state_set_hr_measuring(true);
    state_set_error(0);
    APP_LOGI("HR spot started");

    bool first_publish = true;
    while (!s_suspended && !s_stop_hr && s_state.sensor_present)
    {
        const uint32_t now = k_uptime_get_32();
        if (now - session.started_ms >= HRM_SPOT_WINDOW_MS ||
            (session.first_hr_ms != 0u && now - session.first_hr_ms >= HRM_SPOT_SETTLE_MS))
        {
            break;
        }

        posix_hrm_frame_t frame;
        if (posix_ioctl(s_fd, POSIX_HRM_IOCTL_READ_FRAME, &frame) != POSIX_OK)
        {
            state_set_error(-EIO);
            APP_LOGW("HR frame read failed");
            break;
        }

        if (frame.wear_changed)
        {
            session.worn = frame.worn;
            Algo_Init();
            if (!session.worn)
            {
                publish_hr(0u, false);
                APP_LOGI("HR sensor not worn (ps=%u env=%u)", frame.ps, frame.env);
            }
        }

        if (session.worn && frame.count > 0u)
        {
            AlgoOutputData_t out;
            run_algorithm(&frame, &out);
            const int32_t hr = out.hrData;
            session.last_p2p = frame_p2p(&frame);
            session.pulse = pulse_vote(&session, session.last_p2p);

            if (session.pulse && hr > 0 && hr < 256)
            {
                publish_hr((uint8_t)hr, true);
                if (session.first_hr_ms == 0u)
                {
                    session.first_hr_ms = now;
                    APP_LOGI("HR first value %ld bpm, rel=%ld p2p=%u",
                             (long)hr, (long)out.reliability,
                             (unsigned)session.last_p2p);
                }
                first_publish = false;
            }
            else if (!first_publish)
            {
                publish_hr(0u, false);
            }
        }

        session.polls++;
        if ((session.polls % 10u) == 0u)
        {
            APP_LOGI("HR worn=%d n=%u ppg=%u p2p=%u ps=%u pulse=%d",
                     (int)session.worn, frame.count,
                     frame.count ? (unsigned)(frame.samples[frame.count - 1u] & 0x0FFFu) : 0u,
                     (unsigned)session.last_p2p, frame.ps, (int)session.pulse);
        }
        os_delay(800u);
    }

    close_sensor();
    if (session.first_hr_ms == 0u)
    {
        publish_hr(0u, false);
    }
    state_set_hr_measuring(false);
    s_stop_hr = false;
    s_next_hr_ms = k_uptime_get_32() + HRM_SPOT_PERIOD_MS;
    APP_LOGI("HR spot stopped, next in %u s", (unsigned)(HRM_SPOT_PERIOD_MS / 1000u));
}

static void spo2_spot(void)
{
    if (open_mode(POSIX_HRM_MODE_SPO2) != 0)
    {
        state_set_error(-EIO);
        APP_LOGW("SpO2 open failed");
        return;
    }

    state_set_spo2_measuring(true);
    posix_hrm_spo2_t sample = {0};
    bool ok = false;
    for (uint8_t i = 0; i < 10u && !s_suspended && !s_stop_spo2; i++)
    {
        if (posix_ioctl(s_fd, POSIX_HRM_IOCTL_READ_SPO2, &sample) == POSIX_OK &&
            (sample.red != 0u || sample.ir != 0u))
        {
            ok = true;
            break;
        }
        os_delay(100u);
    }
    close_sensor();

    if (ok)
    {
        publish_spo2_raw(sample.red, sample.ir);
        APP_LOGI("SpO2 raw red=%u ir=%u", sample.red, sample.ir);
    }
    else
    {
        state_set_error(-EIO);
        APP_LOGW("SpO2 raw acquisition failed");
    }
    state_set_spo2_measuring(false);
    s_stop_spo2 = false;
    s_next_spo2_ms = k_uptime_get_32() + HRM_SPOT_PERIOD_MS;
}

static void hrm_task(void *param)
{
    (void)param;
    (void)posix_port_init_all();
    while (s_fd == POSIX_FD_NULL)
    {
        s_fd = posix_open(HRM_DEVICE_PATH);
        if (s_fd == POSIX_FD_NULL)
        {
            state_set_error(-ENODEV);
            APP_LOGW("cannot open %s, retrying", TRACE_STRING(HRM_DEVICE_PATH));
            os_delay(5000u);
        }
    }

    s_next_hr_ms = k_uptime_get_32() + HRM_FIRST_SPOT_MS;
    s_next_spo2_ms = k_uptime_get_32() + HRM_FIRST_SPO2_MS;

    for (;;)
    {
        if (s_suspended)
        {
            s_hr_requested = false;
            s_spo2_requested = false;
            os_delay(200u);
            continue;
        }

        if (!s_state.sensor_present && !probe_sensor())
        {
            s_next_hr_ms = k_uptime_get_32() + 30000u;
            s_next_spo2_ms = s_next_hr_ms;
            os_delay(500u);
            continue;
        }

        const uint32_t now = k_uptime_get_32();
        if (s_hr_requested || time_reached(now, s_next_hr_ms))
        {
            s_hr_requested = false;
            hr_spot();
            continue;
        }

        if (s_spo2_requested || time_reached(now, s_next_spo2_ms))
        {
            s_spo2_requested = false;
            spo2_spot();
            continue;
        }

        os_delay(200u);
    }
}

int health_hrm_start(void)
{
    if (s_task != NULL)
    {
        return 0;
    }
    memset(&s_state, 0, sizeof(s_state));
    if (!os_task_create(&s_task, "hrm", hrm_task, NULL,
                        HRM_TASK_STACK, HRM_TASK_PRIO))
    {
        s_task = NULL;
        return -ENOMEM;
    }
    return 0;
}

void health_hrm_set_suspended(bool suspended)
{
    s_suspended = suspended;
    if (suspended)
    {
        s_stop_hr = true;
        s_stop_spo2 = true;
    }
    APP_LOGI("sensor %s", TRACE_STRING(suspended ? "suspended" : "resumed"));
}

void health_hrm_start_heart_rate(void)
{
    s_hr_requested = true;
    s_stop_hr = false;
}

void health_hrm_stop_heart_rate(void)
{
    s_stop_hr = true;
    s_hr_requested = false;
}

void health_hrm_start_spo2(void)
{
    s_spo2_requested = true;
    s_stop_spo2 = false;
}

void health_hrm_stop_spo2(void)
{
    s_stop_spo2 = true;
    s_spo2_requested = false;
}

void health_hrm_get_state(app_health_hrm_state_t *out)
{
    if (out == NULL)
    {
        return;
    }
    uint32_t key = os_lock();
    *out = s_state;
    os_unlock(key);
}
