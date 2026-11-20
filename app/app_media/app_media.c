/* Phone A2DP SBC -> Realtek DSP -> watch loudspeaker, plus local MIC capture. */
#include "app_media.h"

#if defined(CONFIG_RTK_BR_PROFILE_A2DP)
#include <stddef.h>
#include <string.h>
#include <zephyr/kernel.h>
#include "app_timer.h"
#include "audio.h"
#include "audio_plugin.h"
#include "audio_route.h"
#include "audio_track.h"
#include "audio_volume.h"
#include "bt_a2dp.h"
#include "hal_gpio.h"
#include "hmi_bt_a2dp.h"
#include "hmi_bt_avrcp.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "trace.h"
#include "gui_api_os.h"

#define MEDIA_LATENCY_MS 120
#define SPEAKER_AMP_PIN P1_1

#define RECORD_FRAME_SAMPLES 240u
#define RECORD_READ_BYTES    2048u
#define RECORD_MAX_BYTES     (64u * 1024u)

static T_AUDIO_PLUGIN_HANDLE plugin;
static uint8_t timer_module;
static uint8_t timer_index;
static void *amp_context;
static T_AUDIO_TRACK_HANDLE track;
static bool audio_ready;
static bool started;
static uint8_t volume = 8;
static unsigned packets;
static unsigned dropped;

static T_AUDIO_TRACK_HANDLE record_track;
static struct k_mutex record_lock;
static struct k_work record_stop_work;
static struct k_work record_start_work;
static uint8_t *record_saved;
static uint8_t *record_read_buf;
static uint32_t record_id;
static uint32_t record_bytes;
static bool record_running;
static bool record_available;
static bool record_ready;

static void amp_timer(uint8_t event, uint16_t param)
{
    (void)event;
    (void)param;
    app_stop_timer(&timer_index);
    if (amp_context != NULL)
    {
        void *context = amp_context;
        amp_context = NULL;
        audio_plugin_msg_send(plugin, context);
    }
}

static void amp_on(T_AUDIO_PLUGIN_HANDLE handle, T_AUDIO_PLUGIN_PARAM param, void *context)
{
    (void)handle;
    (void)param;
    if (hal_gpio_set_level(SPEAKER_AMP_PIN, GPIO_LEVEL_HIGH) != GPIO_STATUS_OK)
    {
        APP_PRINT_ERROR0("media: amplifier enable failed");
    }
    amp_context = context;
    if (app_start_timer(&timer_index, "media_amp", timer_module, 0, 0, false, 200) != 0)
    {
        amp_timer(0, 0);
    }
}

static void amp_digital_on(T_AUDIO_PLUGIN_HANDLE handle, T_AUDIO_PLUGIN_PARAM param,
                           void *context)
{
    (void)handle;
    (void)param;
    audio_plugin_msg_send(plugin, context);
}

static void amp_off(T_AUDIO_PLUGIN_HANDLE handle, T_AUDIO_PLUGIN_PARAM param, void *context)
{
    (void)handle;
    (void)param;
    app_stop_timer(&timer_index);
    amp_context = NULL;
    (void)hal_gpio_set_level(SPEAKER_AMP_PIN, GPIO_LEVEL_LOW);
    audio_plugin_msg_send(plugin, context);
}

static void release_track(void)
{
    started = false;
    if (track != NULL)
    {
        audio_track_release(track);
        track = NULL;
    }
}

static void record_limit_work(struct k_work *work)
{
    (void)work;
    app_media_record_stop();
}

static void record_start_work_handler(struct k_work *work)
{
    (void)work;
    if (!app_media_record_start())
    {
        APP_PRINT_WARN0("media: MIC deferred start failed");
    }
}

static void record_audio_event(T_AUDIO_EVENT event, T_AUDIO_EVENT_PARAM *param,
                               uint16_t len)
{
    if (record_track == NULL || param == NULL)
    {
        return;
    }

    if (event == AUDIO_EVENT_TRACK_STATE_CHANGED &&
        len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_STATE_CHANGED) &&
        param->track_state_changed.handle == record_track)
    {
        T_AUDIO_TRACK_STATE state = param->track_state_changed.state;
        record_running = state == AUDIO_TRACK_STATE_STARTED ||
                         state == AUDIO_TRACK_STATE_RESTARTED;
        APP_PRINT_INFO2("media: MIC track state=%d, cause=%d", state,
                        param->track_state_changed.cause);
    }
    else if (event == AUDIO_EVENT_TRACK_DATA_IND &&
             len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_DATA_IND) &&
             param->track_data_ind.handle == record_track && record_running)
    {
        uint16_t requested = param->track_data_ind.len;
        if (record_read_buf == NULL || requested > RECORD_READ_BYTES)
        {
            return;
        }

        uint32_t timestamp = 0;
        uint16_t seq = 0;
        uint16_t received = 0;
        uint8_t frames = 0;
        T_AUDIO_STREAM_STATUS status;
        if (!audio_track_read(record_track, &timestamp, &seq, &status, &frames,
                              record_read_buf, requested, &received) ||
            received == 0u)
        {
            return;
        }

        if (k_mutex_lock(&record_lock, K_NO_WAIT) == 0)
        {
            uint32_t space = RECORD_MAX_BYTES - record_bytes;
            uint16_t copy = received < space ? received : (uint16_t)space;
            if (copy != 0u)
            {
                memcpy(record_saved + record_bytes, record_read_buf, copy);
                record_bytes += copy;
            }
            if (copy < received || record_bytes == RECORD_MAX_BYTES)
            {
                record_running = false;
                k_work_submit(&record_stop_work);
            }
            k_mutex_unlock(&record_lock);
        }
    }
    else if (event == AUDIO_EVENT_TRACK_ERROR_REPORT &&
             len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_ERROR_REPORT) &&
             param->track_error_report.handle == record_track)
    {
        record_running = false;
        APP_PRINT_ERROR0("media: MIC track error");
    }
}

static bool sbc_format(T_AUDIO_FORMAT_INFO *format, const T_HMI_A2DP_CODEC_SBC *sbc)
{
    memset(format, 0, sizeof(*format));
    format->type = AUDIO_FORMAT_TYPE_SBC;
    format->frame_num = 5;
    switch (sbc->sampling_frequency)
    {
    case BT_A2DP_SBC_SAMPLING_FREQUENCY_44_1KHZ: format->attr.sbc.sample_rate = 44100; break;
    case BT_A2DP_SBC_SAMPLING_FREQUENCY_48KHZ: format->attr.sbc.sample_rate = 48000; break;
    default: return false;
    }
    switch (sbc->channel_mode)
    {
    case BT_A2DP_SBC_CHANNEL_MODE_MONO:
        format->attr.sbc.chann_mode = AUDIO_SBC_CHANNEL_MODE_MONO;
        format->attr.sbc.chann_location = AUDIO_CHANNEL_LOCATION_MONO;
        break;
    case BT_A2DP_SBC_CHANNEL_MODE_DUAL_CHANNEL:
        format->attr.sbc.chann_mode = AUDIO_SBC_CHANNEL_MODE_DUAL;
        break;
    case BT_A2DP_SBC_CHANNEL_MODE_STEREO:
        format->attr.sbc.chann_mode = AUDIO_SBC_CHANNEL_MODE_STEREO;
        break;
    case BT_A2DP_SBC_CHANNEL_MODE_JOINT_STEREO:
        format->attr.sbc.chann_mode = AUDIO_SBC_CHANNEL_MODE_JOINT_STEREO;
        break;
    default: return false;
    }
    if (format->attr.sbc.chann_location != AUDIO_CHANNEL_LOCATION_MONO)
    {
        format->attr.sbc.chann_location = AUDIO_CHANNEL_LOCATION_FL | AUDIO_CHANNEL_LOCATION_FR;
    }
    switch (sbc->block_length)
    {
    case BT_A2DP_SBC_BLOCK_LENGTH_4: format->attr.sbc.block_length = 4; break;
    case BT_A2DP_SBC_BLOCK_LENGTH_8: format->attr.sbc.block_length = 8; break;
    case BT_A2DP_SBC_BLOCK_LENGTH_12: format->attr.sbc.block_length = 12; break;
    case BT_A2DP_SBC_BLOCK_LENGTH_16: format->attr.sbc.block_length = 16; break;
    default: return false;
    }
    switch (sbc->subbands)
    {
    case BT_A2DP_SBC_SUBBANDS_4: format->attr.sbc.subband_num = 4; break;
    case BT_A2DP_SBC_SUBBANDS_8: format->attr.sbc.subband_num = 8; break;
    default: return false;
    }
    format->attr.sbc.allocation_method =
        sbc->allocation_method == BT_A2DP_SBC_ALLOCATION_METHOD_SNR ? 1 : 0;
    return true;
}

static bool on_sink_start(uint8_t *addr, T_HMI_A2DP_CODEC_SBC *codec)
{
    (void)addr;
    if (!audio_ready)
    {
        return false;
    }

    T_AUDIO_FORMAT_INFO format;
    release_track();
    if (!sbc_format(&format, codec))
    {
        APP_PRINT_ERROR0("media: unsupported SBC configuration");
        return false;
    }

    track = audio_track_create(AUDIO_STREAM_TYPE_PLAYBACK, AUDIO_STREAM_MODE_NORMAL,
                               AUDIO_STREAM_USAGE_LOCAL, format, volume, 0,
                               AUDIO_DEVICE_OUT_SPK, NULL, NULL);
    if (track == NULL)
    {
        APP_PRINT_ERROR0("media: playback track create failed");
        return false;
    }
    (void)audio_track_latency_set(track, MEDIA_LATENCY_MS, true);

    if (format.attr.sbc.chann_location != AUDIO_CHANNEL_LOCATION_MONO)
    {
        T_AUDIO_ROUTE_PATH path = audio_route_path_take(AUDIO_CATEGORY_AUDIO,
                                                        AUDIO_DEVICE_OUT_SPK);
        for (uint8_t i = 0; path.entry != NULL && i < path.entry_num; ++i)
        {
            if (path.entry[i].endpoint_type == AUDIO_ROUTE_ENDPOINT_SPK &&
                path.entry[i].io_type == AUDIO_ROUTE_IO_AUDIO_PRIMARY_OUT &&
                __builtin_popcount(path.entry[i].gateway_attr.channs) == 1)
            {
                uint32_t mix = AUDIO_CHANNEL_LOCATION_FL | AUDIO_CHANNEL_LOCATION_FR;
                (void)audio_track_channel_out_set(track, 1, &mix);
                break;
            }
        }
        audio_route_path_give(&path);
    }

    packets = 0;
    dropped = 0;
    if (!audio_track_start(track))
    {
        release_track();
        return false;
    }
    uint32_t sample_rate = format.attr.sbc.sample_rate;
    APP_PRINT_INFO1("media: SBC %d Hz playback started", sample_rate);
    return true;
}

static void on_stop(uint8_t *addr)
{
    (void)addr;
    APP_PRINT_INFO2("media: stopped, packets=%d, dropped=%d", packets, dropped);
    release_track();
}

static void on_packet(uint8_t *addr, uint32_t clock, uint16_t seq, uint8_t frames,
                      uint8_t *data, uint16_t len)
{
    (void)addr;
    if (track != NULL)
    {
        uint16_t written = 0;
        bool ok = audio_track_write(track, clock, seq, AUDIO_STREAM_STATUS_CORRECT,
                                    frames, data, len, &written);
        if (!ok || written != len)
        {
            dropped++;
        }
    }
    else
    {
        dropped++;
    }
    if (++packets % 500u == 0u)
    {
        APP_PRINT_INFO2("media: packets=%d, dropped=%d", packets, dropped);
    }
}

static void on_audio(T_AUDIO_EVENT event, void *buf, uint16_t len)
{
    T_AUDIO_EVENT_PARAM *param = buf;
    if (param == NULL)
    {
        return;
    }
    record_audio_event(event, param, len);
    if (event == AUDIO_EVENT_TRACK_STATE_CHANGED &&
        len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_STATE_CHANGED) &&
        param->track_state_changed.handle == track)
    {
        T_AUDIO_TRACK_STATE state = param->track_state_changed.state;
        started = state == AUDIO_TRACK_STATE_STARTED ||
                  state == AUDIO_TRACK_STATE_RESTARTED;
        APP_PRINT_INFO2("media: track state=%d, cause=%d", state,
                        param->track_state_changed.cause);
    }
    else if (event == AUDIO_EVENT_TRACK_ERROR_REPORT &&
             len >= sizeof(T_AUDIO_EVENT_PARAM_TRACK_ERROR_REPORT) &&
             param->track_error_report.handle == track)
    {
        started = false;
        APP_PRINT_ERROR0("media: playback track error");
    }
}

static const int16_t media_dac_gain_table[16] =
{
    (int16_t)0xc000, (int16_t)0xeb00, (int16_t)0xec80, (int16_t)0xee00,
    (int16_t)0xef80, (int16_t)0xf100, (int16_t)0xf280, (int16_t)0xf400,
    (int16_t)0xf580, (int16_t)0xf700, (int16_t)0xf880, (int16_t)0xfa00,
    (int16_t)0xfb80, (int16_t)0xfd00, (int16_t)0xfe80, (int16_t)0x0000
};

static bool media_dac_gain_get(T_AUDIO_CATEGORY category, uint32_t level,
                               T_AUDIO_ROUTE_DAC_GAIN *gain)
{
    if (category != AUDIO_CATEGORY_AUDIO || gain == NULL)
    {
        return false;
    }
    if (level > 15u)
    {
        level = 15u;
    }
    gain->left_gain = media_dac_gain_table[level];
    gain->right_gain = media_dac_gain_table[level];
    return true;
}

static void on_volume(uint8_t *addr, uint8_t level)
{
    (void)addr;
    uint8_t next = (uint8_t)(((unsigned)level * 15u + 63u) / 127u);
    if (next == volume)
    {
        return;
    }
    volume = next;
    (void)audio_volume_out_set(AUDIO_STREAM_TYPE_PLAYBACK, volume);
    if (track != NULL)
    {
        (void)audio_track_volume_out_set(track, volume);
    }
    APP_PRINT_INFO2("media: speaker volume %d/15 (avrcp %d)", volume, level);
}

void app_media_audio_ready(void)
{
    static const T_AUDIO_PLUGIN_POLICY policies[] = {
        { AUDIO_CATEGORY_AUDIO, AUDIO_PLUGIN_OCCASION_ANALOG_DOMAIN_ON, amp_on },
        { AUDIO_CATEGORY_AUDIO, AUDIO_PLUGIN_OCCASION_DIGITAL_DOMAIN_ON, amp_digital_on },
        { AUDIO_CATEGORY_AUDIO, AUDIO_PLUGIN_OCCASION_DIGITAL_DOMAIN_OFF, amp_off },
        { AUDIO_CATEGORY_VOICE, AUDIO_PLUGIN_OCCASION_ANALOG_DOMAIN_ON, amp_on },
        { AUDIO_CATEGORY_VOICE, AUDIO_PLUGIN_OCCASION_DIGITAL_DOMAIN_ON, amp_digital_on },
        { AUDIO_CATEGORY_VOICE, AUDIO_PLUGIN_OCCASION_DIGITAL_DOMAIN_OFF, amp_off },
    };

    if (app_timer_reg_cb(amp_timer, &timer_module) != 0)
    {
        APP_PRINT_ERROR0("media: amplifier timer unavailable");
        return;
    }
    plugin = audio_plugin_create(policies, sizeof(policies) / sizeof(policies[0]));
    if (plugin == NULL || !audio_mgr_cback_register(on_audio))
    {
        APP_PRINT_ERROR0("media: audio plugin/callback init failed");
        return;
    }
    (void)audio_route_gain_register(AUDIO_CATEGORY_AUDIO, media_dac_gain_get, NULL);
    (void)audio_volume_out_max_set(AUDIO_STREAM_TYPE_PLAYBACK, 15);
    (void)audio_volume_out_min_set(AUDIO_STREAM_TYPE_PLAYBACK, 0);
    (void)audio_volume_out_set(AUDIO_STREAM_TYPE_PLAYBACK, volume);
    audio_ready = true;
    k_mutex_init(&record_lock);
    k_work_init(&record_stop_work, record_limit_work);
    k_work_init(&record_start_work, record_start_work_handler);
    record_ready = true;
    APP_PRINT_INFO1("media: audio ready, volume %d/15", volume);
}

bool app_media_record_start(void)
{
    if (!record_ready)
    {
        return false;
    }

    k_mutex_lock(&record_lock, K_FOREVER);
    if (record_track != NULL || track != NULL)
    {
        k_mutex_unlock(&record_lock);
        return false;
    }

    T_AUDIO_FORMAT_INFO format = {0};
    format.type = AUDIO_FORMAT_TYPE_PCM;
    format.frame_num = 1;
    format.attr.pcm.sample_rate = 16000;
    format.attr.pcm.frame_length = RECORD_FRAME_SAMPLES;
    format.attr.pcm.chann_num = 1;
    format.attr.pcm.bit_width = 16;
    format.attr.pcm.chann_location = AUDIO_CHANNEL_LOCATION_MONO;

    if (record_saved != NULL)
    {
        gui_free(record_saved);
        record_saved = NULL;
    }
    record_saved = gui_malloc(RECORD_MAX_BYTES);
    record_read_buf = gui_malloc(RECORD_READ_BYTES);
    record_bytes = 0;
    record_available = false;
    record_running = false;
    if (record_saved == NULL || record_read_buf == NULL)
    {
        gui_free(record_saved);
        gui_free(record_read_buf);
        record_saved = NULL;
        record_read_buf = NULL;
        k_mutex_unlock(&record_lock);
        return false;
    }

    record_track = audio_track_create(AUDIO_STREAM_TYPE_RECORD, AUDIO_STREAM_MODE_NORMAL,
                                      AUDIO_STREAM_USAGE_LOCAL, format, 0, 10,
                                      AUDIO_DEVICE_IN_MIC, NULL, NULL);
    if (record_track == NULL)
    {
        gui_free(record_saved);
        gui_free(record_read_buf);
        record_saved = NULL;
        record_read_buf = NULL;
        k_mutex_unlock(&record_lock);
        return false;
    }

    T_AUDIO_TRACK_HANDLE handle = record_track;
    k_mutex_unlock(&record_lock);
    if (!audio_track_start(handle))
    {
        k_mutex_lock(&record_lock, K_FOREVER);
        record_track = NULL;
        gui_free(record_saved);
        gui_free(record_read_buf);
        record_saved = NULL;
        record_read_buf = NULL;
        k_mutex_unlock(&record_lock);
        audio_track_release(handle);
        return false;
    }

    APP_PRINT_INFO0("media: MIC capture started (PCM 16 kHz mono 16-bit)");
    return true;
}

bool app_media_record_start_from_ui(void)
{
    if (!record_ready || app_media_record_active())
    {
        return false;
    }
    return k_work_submit(&record_start_work) >= 0;
}

void app_media_record_stop(void)
{
    if (!record_ready)
    {
        return;
    }

    (void)k_work_cancel(&record_start_work);
    k_mutex_lock(&record_lock, K_FOREVER);
    T_AUDIO_TRACK_HANDLE handle = record_track;
    record_track = NULL;
    record_running = false;
    record_available = handle != NULL && record_bytes != 0u;
    if (record_available)
    {
        record_id++;
    }
    gui_free(record_read_buf);
    record_read_buf = NULL;
    if (!record_available)
    {
        gui_free(record_saved);
        record_saved = NULL;
    }
    k_mutex_unlock(&record_lock);

    if (handle != NULL)
    {
        audio_track_release(handle);
        APP_PRINT_INFO2("media: MIC capture stopped, id=%u bytes=%u",
                        record_id, record_bytes);
    }
}

bool app_media_record_active(void)
{
    return record_track != NULL;
}

bool app_media_record_info(uint32_t *id, uint32_t *bytes)
{
    if (!record_ready || id == NULL || bytes == NULL)
    {
        return false;
    }
    k_mutex_lock(&record_lock, K_FOREVER);
    bool ok = record_available && record_saved != NULL;
    *id = ok ? record_id : 0u;
    *bytes = ok ? record_bytes : 0u;
    k_mutex_unlock(&record_lock);
    return ok;
}

static uint16_t record_copy(uint32_t expected_id, uint32_t offset, uint8_t *out,
                            uint16_t len, bool live)
{
    if (!record_ready || out == NULL || len == 0u)
    {
        return 0;
    }
    k_mutex_lock(&record_lock, K_FOREVER);
    uint16_t copied = 0;
    if (record_saved != NULL && (!live ? expected_id == record_id && record_available : true) &&
        offset < record_bytes)
    {
        uint32_t remain = record_bytes - offset;
        copied = remain < len ? (uint16_t)remain : len;
        memcpy(out, record_saved + offset, copied);
    }
    k_mutex_unlock(&record_lock);
    return copied;
}

uint16_t app_media_record_read(uint32_t id, uint32_t offset, uint8_t *out, uint16_t len)
{
    return record_copy(id, offset, out, len, false);
}

uint32_t app_media_record_live_bytes(void)
{
    if (!record_ready)
    {
        return 0;
    }
    k_mutex_lock(&record_lock, K_FOREVER);
    uint32_t bytes = record_saved != NULL ? record_bytes : 0u;
    k_mutex_unlock(&record_lock);
    return bytes;
}

uint16_t app_media_record_live_read(uint32_t offset, uint8_t *out, uint16_t len)
{
    return record_copy(0u, offset, out, len, true);
}

bool app_media_speaker_free(void)
{
    return audio_ready && track == NULL && !app_media_record_active();
}

app_media_state_t app_media_state(void)
{
    hmi_media_snapshot_t snapshot;
    hmi_media_snapshot(&snapshot);
    if (!snapshot.connected)
    {
        return APP_MEDIA_STATE_IDLE;
    }
    return snapshot.playing ? APP_MEDIA_STATE_PLAYING : APP_MEDIA_STATE_PAUSED;
}

bool app_media_connected(void)
{
    hmi_media_snapshot_t snapshot;
    hmi_media_snapshot(&snapshot);
    return snapshot.connected;
}

int app_media_play(void)
{
    if (app_media_state() == APP_MEDIA_STATE_PLAYING)
    {
        return 0;
    }
    return hmi_media_send(HMI_MEDIA_TOGGLE) ? 0 : -1;
}

int app_media_pause(void)
{
    if (app_media_state() != APP_MEDIA_STATE_PLAYING)
    {
        return 0;
    }
    return hmi_media_send(HMI_MEDIA_TOGGLE) ? 0 : -1;
}

int app_media_next(void) { return hmi_media_send(HMI_MEDIA_NEXT) ? 0 : -1; }
int app_media_prev(void) { return hmi_media_send(HMI_MEDIA_PREVIOUS) ? 0 : -1; }

int app_media_volume_set(uint8_t percent)
{
    return hmi_media_set_volume(percent) ? 0 : -1;
}

uint8_t app_media_volume_get(void)
{
    return (uint8_t)(((unsigned)volume * 100u + 7u) / 15u);
}

const char *app_media_track_title(void)
{
    static char title[HMI_MEDIA_NAME_MAX];
    hmi_media_snapshot_t snapshot;
    hmi_media_snapshot(&snapshot);
    memcpy(title, snapshot.title, sizeof(title));
    title[sizeof(title) - 1u] = '\0';
    return title;
}

const char *app_media_track_artist(void)
{
    static char artist[HMI_MEDIA_NAME_MAX];
    hmi_media_snapshot_t snapshot;
    hmi_media_snapshot(&snapshot);
    memcpy(artist, snapshot.artist, sizeof(artist));
    artist[sizeof(artist) - 1u] = '\0';
    return artist;
}

static int media_init(void)
{
    if (hal_gpio_init_pin(SPEAKER_AMP_PIN, GPIO_TYPE_AUTO, GPIO_DIR_OUTPUT,
                          GPIO_PULL_NONE) != GPIO_STATUS_OK ||
        hal_gpio_set_level(SPEAKER_AMP_PIN, GPIO_LEVEL_LOW) != GPIO_STATUS_OK)
    {
        APP_PRINT_ERROR0("media: speaker amplifier GPIO init failed");
        return -1;
    }
    hmi_bt_a2dp_set_sink_start_cb(on_sink_start);
    hmi_bt_a2dp_set_stream_stop_cb(on_stop);
    hmi_bt_a2dp_set_packet_cb(on_packet);
#ifdef CONFIG_RTK_BR_PROFILE_AVRCP
    hmi_bt_avrcp_set_volume_cb(on_volume);
#endif
    return 0;
}

#else

static int media_init(void) { return 0; }
void app_media_audio_ready(void) {}
app_media_state_t app_media_state(void) { return APP_MEDIA_STATE_IDLE; }
bool app_media_connected(void) { return false; }
int app_media_play(void) { return -1; }
int app_media_pause(void) { return -1; }
int app_media_next(void) { return -1; }
int app_media_prev(void) { return -1; }
int app_media_volume_set(uint8_t percent) { (void)percent; return -1; }
uint8_t app_media_volume_get(void) { return 0; }
const char *app_media_track_title(void) { return ""; }
const char *app_media_track_artist(void) { return ""; }
bool app_media_record_start(void) { return false; }
bool app_media_record_start_from_ui(void) { return false; }
void app_media_record_stop(void) {}
bool app_media_record_active(void) { return false; }
bool app_media_record_info(uint32_t *id, uint32_t *bytes)
{
    (void)id; (void)bytes; return false;
}
uint16_t app_media_record_read(uint32_t id, uint32_t offset, uint8_t *out, uint16_t len)
{
    (void)id; (void)offset; (void)out; (void)len; return 0;
}
uint32_t app_media_record_live_bytes(void) { return 0; }
uint16_t app_media_record_live_read(uint32_t offset, uint8_t *out, uint16_t len)
{
    (void)offset; (void)out; (void)len; return 0;
}
bool app_media_speaker_free(void) { return false; }

#endif

const app_module_t app_media_module =
{
    .name = "media",
    .init = media_init,
};
