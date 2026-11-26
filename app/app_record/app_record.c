#include "app_record.h"

#include "app_event.h"
#include "app_event_defs.h"
#include "app_media.h"
#include "audio.h"
#include "audio_track.h"
#include "gui_api_os.h"
#include "honey_protocol.h"
#include "honey_protocol_log.h"
#include "trace.h"
#include "watch_l2_cmd_record.h"

#include <string.h>
#include <zephyr/kernel.h>

#define RECORD_READ_BYTES    2048u
#define RECORD_STREAM_BYTES  1920u
#define RECORD_DATA_HEADER_LEN 8u

static T_AUDIO_TRACK_HANDLE s_record_track;
static struct k_mutex s_record_lock;
static struct k_work s_record_stop_work;
static uint8_t *s_record_read_buf;
static bool s_record_running;
static bool s_record_initialized;
static bool s_audio_ready;
static bool s_stream_enabled;
static uint32_t s_stream_start_ms;
static uint32_t s_stream_bytes;
static uint8_t s_stream_tx_buf[RECORD_STREAM_BYTES];
static uint16_t s_stream_tx_len;
static uint32_t s_stream_tx_timestamp;
static uint8_t s_data_event_logs;
static uint8_t s_stream_frame[2u + 3u + RECORD_DATA_HEADER_LEN + 2u +
                              RECORD_STREAM_BYTES];
static uint16_t s_stream_seq;
static uint8_t s_stream_log_count;
static volatile uint8_t s_pending_stop_reason;

static void app_record_stop_internal(app_record_stop_reason_t reason);

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static bool record_notify_started(void)
{
    uint8_t frame[2u + 3u + 13u];
    uint8_t value[13];
    uint16_t pos = 0u;

    value[0] = WATCH_L2_RECORD_CODEC_PCM;
    put_u32(&value[1], APP_RECORD_PCM_SAMPLE_RATE);
    value[5] = 1u;
    value[6] = (uint8_t)(APP_RECORD_PCM_FRAME_SAMPLES * 1000u /
                         APP_RECORD_PCM_SAMPLE_RATE);
    put_u16(&value[7], 0u);
    value[9] = 1u;
    value[10] = 16u;
    put_u16(&value[11], APP_RECORD_PCM_FRAME_SAMPLES * sizeof(int16_t));

    frame[pos++] = WATCH_L2_CMD_RECORD;
    frame[pos++] = WATCH_L2_RECORD_VERSION << 4;
    frame[pos++] = WATCH_L2_RECORD_KEY_START;
    put_u16(&frame[pos], sizeof(value));
    pos += 2u;
    memcpy(&frame[pos], value, sizeof(value));
    pos += sizeof(value);
    return honey_protocol_send_with_ack(frame, pos);
}

static bool record_notify_stopped(uint8_t reason)
{
    uint8_t frame[2u + 3u + 1u];
    uint16_t pos = 0u;

    frame[pos++] = WATCH_L2_CMD_RECORD;
    frame[pos++] = WATCH_L2_RECORD_VERSION << 4;
    frame[pos++] = WATCH_L2_RECORD_KEY_STOP;
    put_u16(&frame[pos], 1u);
    pos += 2u;
    frame[pos++] = reason;
    return honey_protocol_send_with_ack(frame, pos);
}

static bool record_send_pcm(uint32_t timestamp, const uint8_t *data,
                            uint16_t len)
{
    if (data == NULL || len == 0u || len > RECORD_STREAM_BYTES)
    {
        return false;
    }

    uint16_t pos = 0u;
    s_stream_frame[pos++] = WATCH_L2_CMD_RECORD;
    s_stream_frame[pos++] = WATCH_L2_RECORD_VERSION << 4;
    s_stream_frame[pos++] = WATCH_L2_RECORD_KEY_DATA;
    put_u16(&s_stream_frame[pos], RECORD_DATA_HEADER_LEN + 2u + len);
    pos += 2u;
    put_u16(&s_stream_frame[pos], s_stream_seq++);
    pos += 2u;
    put_u32(&s_stream_frame[pos], timestamp);
    pos += 4u;
    s_stream_frame[pos++] = 1u;
    s_stream_frame[pos++] = 0u;
    put_u16(&s_stream_frame[pos], len);
    pos += 2u;
    memcpy(&s_stream_frame[pos], data, len);
    pos += len;

    if (s_stream_log_count < 3u)
    {
        s_stream_log_count++;
        HONEY_PROTOCOL_LOG("record PCM send seq=%u len=%u",
                           (unsigned)(s_stream_seq - 1u), (unsigned)len);
    }
    return honey_protocol_send_without_ack(s_stream_frame, pos);
}

static void record_stream_flush(void)
{
    if (s_stream_tx_len == 0u)
    {
        return;
    }
    (void)record_send_pcm(s_stream_tx_timestamp, s_stream_tx_buf,
                          s_stream_tx_len);
    s_stream_bytes += s_stream_tx_len;
    s_stream_tx_len = 0u;
}

static void record_stream_write(uint32_t timestamp, const uint8_t *data,
                                uint16_t len)
{
    while (len > 0u)
    {
        if (s_stream_tx_len == 0u)
        {
            s_stream_tx_timestamp = timestamp;
        }
        uint16_t space = RECORD_STREAM_BYTES - s_stream_tx_len;
        uint16_t copy = len < space ? len : space;
        memcpy(s_stream_tx_buf + s_stream_tx_len, data, copy);
        s_stream_tx_len += copy;
        data += copy;
        len -= copy;
        timestamp += copy / APP_RECORD_PCM_BYTES_PER_MS;
        if (s_stream_tx_len == RECORD_STREAM_BYTES)
        {
            record_stream_flush();
        }
    }
}

static void record_limit_work(struct k_work *work)
{
    (void)work;
    app_record_stop_internal((app_record_stop_reason_t)s_pending_stop_reason);
}

static void on_record_start_req(app_event_id_t id, const void *payload,
                                size_t len, void *user)
{
    (void)id;
    (void)payload;
    (void)len;
    (void)user;

    if (app_record_is_active())
    {
        return;
    }
    if (!app_record_start())
    {
        (void)record_notify_stopped(APP_RECORD_STOP_REQUEST_REJECTED);
        return;
    }
    if (!record_notify_started())
    {
        app_record_stop_internal(APP_RECORD_STOP_TRANSPORT_ERROR);
        return;
    }
    s_stream_start_ms = k_uptime_get_32();
    s_stream_enabled = true;
}

static void on_record_stop_req(app_event_id_t id, const void *payload,
                               size_t len, void *user)
{
    (void)id;
    (void)user;
    if (payload != NULL && len == sizeof(app_evt_record_stop_t))
    {
        const app_evt_record_stop_t *request = payload;
        app_record_stop_internal((app_record_stop_reason_t)request->reason);
    }
}

int app_record_init(void)
{
    if (s_record_initialized)
    {
        return 0;
    }

    k_mutex_init(&s_record_lock);
    k_work_init(&s_record_stop_work, record_limit_work);
    if (app_event_subscribe(EVT_RECORD_START_REQ, on_record_start_req, NULL) != 0 ||
        app_event_subscribe(EVT_RECORD_STOP_REQ, on_record_stop_req, NULL) != 0)
    {
        APP_PRINT_ERROR0("record: event subscription failed");
        return -1;
    }
    s_record_initialized = true;
    APP_PRINT_INFO0("record: events registered");
    return 0;
}

void app_record_audio_ready(void)
{
    if (!s_record_initialized)
    {
        APP_PRINT_ERROR0("record: audio ready before event registration");
        return;
    }
    s_audio_ready = true;
    APP_PRINT_INFO0("record: audio ready");
}

void app_record_handle_audio_event(uint32_t event, void *param_ptr, uint16_t len)
{
    T_AUDIO_EVENT_PARAM *param = param_ptr;
    if (s_record_track == NULL || param == NULL)
    {
        return;
    }

    if (event == AUDIO_EVENT_TRACK_STATE_CHANGED &&
        len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_STATE_CHANGED) &&
        param->track_state_changed.handle == s_record_track)
    {
        T_AUDIO_TRACK_STATE state = param->track_state_changed.state;
        s_record_running = state == AUDIO_TRACK_STATE_STARTED ||
                           state == AUDIO_TRACK_STATE_RESTARTED;
        APP_PRINT_INFO2("record: MIC track state=%d, cause=%d", state,
                        param->track_state_changed.cause);
    }
    else if (event == AUDIO_EVENT_TRACK_DATA_IND &&
             len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_DATA_IND) &&
             param->track_data_ind.handle == s_record_track && s_record_running)
    {
        uint16_t requested = param->track_data_ind.len;
        if (s_record_read_buf == NULL || requested > RECORD_READ_BYTES)
        {
            return;
        }

        uint32_t timestamp = 0;
        uint16_t seq = 0;
        uint16_t received = 0;
        uint8_t frames = 0;
        T_AUDIO_STREAM_STATUS status;
        if (!audio_track_read(s_record_track, &timestamp, &seq, &status, &frames,
                              s_record_read_buf, requested, &received) ||
            received == 0u)
        {
            return;
        }

        if (s_data_event_logs < 5u)
        {
            s_data_event_logs++;
            HONEY_PROTOCOL_LOG("record PCM event req=%u got=%u enabled=%u",
                               (unsigned)requested, (unsigned)received,
                               (unsigned)s_stream_enabled);
        }

        if (s_stream_enabled)
        {
            record_stream_write(k_uptime_get_32() - s_stream_start_ms,
                                s_record_read_buf, received);
        }
    }
    else if (event == AUDIO_EVENT_TRACK_ERROR_REPORT &&
             len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_ERROR_REPORT) &&
             param->track_error_report.handle == s_record_track)
    {
        s_record_running = false;
        s_pending_stop_reason = APP_RECORD_STOP_AUDIO_ERROR;
        k_work_submit(&s_record_stop_work);
        APP_PRINT_ERROR0("record: MIC track error");
    }
}

bool app_record_start(void)
{
    if (!s_record_initialized || !s_audio_ready)
    {
        return false;
    }

    k_mutex_lock(&s_record_lock, K_FOREVER);
    if (s_record_track != NULL || app_media_track_active())
    {
        k_mutex_unlock(&s_record_lock);
        return false;
    }

    T_AUDIO_FORMAT_INFO format = {0};
    format.type = AUDIO_FORMAT_TYPE_PCM;
    format.frame_num = 1;
    format.attr.pcm.sample_rate = APP_RECORD_PCM_SAMPLE_RATE;
    format.attr.pcm.frame_length = APP_RECORD_PCM_FRAME_SAMPLES;
    format.attr.pcm.chann_num = 1;
    format.attr.pcm.bit_width = 16;
    format.attr.pcm.chann_location = AUDIO_CHANNEL_LOCATION_MONO;

    s_record_read_buf = gui_malloc(RECORD_READ_BYTES);
    s_record_running = false;
    s_stream_enabled = false;
    s_stream_tx_len = 0u;
    s_stream_bytes = 0u;
    s_stream_seq = 0u;
    s_stream_log_count = 0u;
    if (s_record_read_buf == NULL)
    {
        k_mutex_unlock(&s_record_lock);
        return false;
    }

    s_record_track = audio_track_create(AUDIO_STREAM_TYPE_RECORD, AUDIO_STREAM_MODE_NORMAL,
                                        AUDIO_STREAM_USAGE_LOCAL, format, 0, 10,
                                        AUDIO_DEVICE_IN_MIC, NULL, NULL);
    if (s_record_track == NULL)
    {
        gui_free(s_record_read_buf);
        s_record_read_buf = NULL;
        k_mutex_unlock(&s_record_lock);
        return false;
    }

    T_AUDIO_TRACK_HANDLE handle = s_record_track;
    k_mutex_unlock(&s_record_lock);
    if (!audio_track_start(handle))
    {
        k_mutex_lock(&s_record_lock, K_FOREVER);
        s_record_track = NULL;
        gui_free(s_record_read_buf);
        s_record_read_buf = NULL;
        k_mutex_unlock(&s_record_lock);
        audio_track_release(handle);
        return false;
    }

    APP_PRINT_INFO0("record: MIC capture started (PCM 16 kHz mono 16-bit)");
    return true;
}

void app_record_stop(void)
{
    app_record_stop_internal(APP_RECORD_STOP_COMPLETED);
}

static void app_record_stop_internal(app_record_stop_reason_t reason)
{
    if (!s_record_initialized)
    {
        return;
    }

    s_stream_enabled = false;
    record_stream_flush();

    k_mutex_lock(&s_record_lock, K_FOREVER);
    T_AUDIO_TRACK_HANDLE handle = s_record_track;
    s_record_track = NULL;
    s_record_running = false;
    gui_free(s_record_read_buf);
    s_record_read_buf = NULL;
    k_mutex_unlock(&s_record_lock);

    if (handle != NULL)
    {
        audio_track_release(handle);
        APP_PRINT_INFO1("record: MIC capture stopped, bytes=%u",
                        s_stream_bytes);
        (void)record_notify_stopped((uint8_t)reason);
    }
}

bool app_record_is_active(void)
{
    return s_record_track != NULL;
}

const app_module_t app_record_module =
{
    .name = "record",
    .init = app_record_init,
};
