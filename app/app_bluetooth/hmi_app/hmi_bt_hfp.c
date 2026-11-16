/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#include <string.h>
#include <stdlib.h>
#include <os_sync.h>
#include <audio.h>
#include "hmi_bt_task.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "trace.h"
#include "bt_types.h"
#include "btm.h"
#include "bt_sdp.h"
#include "bt_hfp.h"
#include "hmi_br_edr_link.h"
#include "hmi_bt_hfp.h"

#define HMI_HFP_RFC_CHANN_NUM        1
#define HMI_HSP_RFC_CHANN_NUM        2
#define HMI_HFP_RETIRED_MAX          4u

typedef struct
{
    bool    hf_connected;
    bool    sco_connected;
    uint8_t air_mode;       /* 2=CVSD, 3=mSBC */
} T_HMI_HFP_LINK;

static T_HMI_HFP_LINK hfp_link[MAX_BR_LINK_NUM];

typedef enum { PHONE_DIAL, PHONE_ANSWER, PHONE_HANGUP, PHONE_MUTE, PHONE_ROUTE } phone_op_t;
typedef struct { phone_op_t op; bool value; char number[32]; } phone_cmd_t;

static void *phone_lock;
static hmi_phone_state_t phone_state;
static bool phone_muted;
static bool phone_watch_audio = true;
static uint8_t phone_addr[6];
static char phone_number[32];
static T_AUDIO_TRACK_HANDLE voice_track;
static T_AUDIO_TRACK_HANDLE retired_tracks[HMI_HFP_RETIRED_MAX];
static unsigned retired_count;
static unsigned audio_busy;
static uint16_t voice_seq;

static void (*hfp_call_status_cb)(uint8_t *bd_addr, uint8_t status);
static void (*hfp_sco_conn_cb)(uint8_t *bd_addr, uint8_t air_mode);
static void (*hfp_sco_data_cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len);
static void (*hfp_sco_disconn_cb)(uint8_t *bd_addr);

static void phone_publish(hmi_phone_state_t previous, hmi_phone_state_t current,
                          bool number_changed)
{
    uint8_t state = (uint8_t)current;
    if (previous != current)
    {
        (void)app_event_publish(EVT_PHONE_STATE_CHANGED, &state, sizeof(state));
        if (current == HMI_PHONE_INCOMING && previous != HMI_PHONE_INCOMING)
        {
            (void)app_event_publish(EVT_PHONE_RING, NULL, 0);
        }
    }
    if (number_changed)
    {
        (void)app_event_publish(EVT_PHONE_NUMBER_CHANGED, NULL, 0);
    }
}

static bool phone_update_number(const char *number, size_t max_len)
{
    char copy[sizeof(phone_number)];
    size_t n = 0;
    while (number != NULL && n < max_len && n < sizeof(copy) - 1u && number[n] != '\0')
    {
        n++;
    }
    if (n != 0)
    {
        memcpy(copy, number, n);
    }
    copy[n] = '\0';
    bool changed = strcmp(phone_number, copy) != 0;
    memcpy(phone_number, copy, n + 1u);
    return changed;
}

hmi_phone_state_t hmi_phone_get_state(void)
{
    hmi_phone_state_t state = HMI_PHONE_DISCONNECTED;
    if (phone_lock != NULL && os_mutex_take(phone_lock, 0xFFFFFFFFu))
    {
        state = phone_state;
        os_mutex_give(phone_lock);
    }
    return state;
}

void hmi_phone_get_number(char *out, size_t size)
{
    if (out == NULL || size == 0)
    {
        return;
    }
    out[0] = '\0';
    if (phone_lock != NULL && os_mutex_take(phone_lock, 0xFFFFFFFFu))
    {
        size_t n = strlen(phone_number);
        if (n >= size)
        {
            n = size - 1u;
        }
        memcpy(out, phone_number, n);
        out[n] = '\0';
        os_mutex_give(phone_lock);
    }
}

bool hmi_phone_get_muted(void)
{
    bool muted = false;
    if (phone_lock != NULL && os_mutex_take(phone_lock, 0xFFFFFFFFu))
    {
        muted = phone_muted;
        os_mutex_give(phone_lock);
    }
    return muted;
}

bool hmi_phone_get_watch_audio(void)
{
    bool on_watch = true;
    if (phone_lock != NULL && os_mutex_take(phone_lock, 0xFFFFFFFFu))
    {
        on_watch = phone_watch_audio;
        os_mutex_give(phone_lock);
    }
    return on_watch;
}

static void phone_publish_bool(app_event_id_t id, bool value)
{
    (void)app_event_publish(id, &value, sizeof(value));
}

static bool phone_enqueue(phone_op_t op, const char *number, bool value)
{
    if (phone_lock == NULL || !os_mutex_take(phone_lock, 0xFFFFFFFFu))
    {
        return false;
    }
    bool allowed = phone_state != HMI_PHONE_DISCONNECTED;
    if (op == PHONE_DIAL)
    {
        allowed = allowed && phone_state == HMI_PHONE_IDLE && number != NULL;
    }
    if (op == PHONE_ANSWER)
    {
        allowed = phone_state == HMI_PHONE_INCOMING;
    }
    if (op == PHONE_HANGUP)
    {
        allowed = allowed && phone_state != HMI_PHONE_IDLE;
    }
    if (op == PHONE_ROUTE)
    {
        allowed = allowed && phone_state == HMI_PHONE_ACTIVE;
    }
    os_mutex_give(phone_lock);
    if (!allowed)
    {
        return false;
    }

    phone_cmd_t *cmd = malloc(sizeof(*cmd));
    if (cmd == NULL)
    {
        return false;
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->op = op;
    cmd->value = value;
    if (number != NULL)
    {
        size_t n = strlen(number);
        if (n == 0 || n >= sizeof(cmd->number))
        {
            free(cmd);
            return false;
        }
        for (size_t i = 0; i < n; ++i)
        {
            char c = number[i];
            if (!((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#'))
            {
                free(cmd);
                return false;
            }
        }
        memcpy(cmd->number, number, n + 1u);
    }

    T_IO_MSG msg = {0};
    msg.type = HMI_PHONE_IO_MSG_TYPE;
    msg.u.buf = cmd;
    if (!hmi_send_msg_to_bt_task(&msg))
    {
        free(cmd);
        return false;
    }
    return true;
}

bool hmi_phone_dial(const char *number) { return phone_enqueue(PHONE_DIAL, number, false); }
bool hmi_phone_answer(void) { return phone_enqueue(PHONE_ANSWER, NULL, false); }
bool hmi_phone_hangup(void) { return phone_enqueue(PHONE_HANGUP, NULL, false); }
bool hmi_phone_set_muted(bool muted) { return phone_enqueue(PHONE_MUTE, NULL, muted); }
bool hmi_phone_set_watch_audio(bool on_watch) { return phone_enqueue(PHONE_ROUTE, NULL, on_watch); }

void hmi_phone_handle_command(void *command)
{
    phone_cmd_t *cmd = command;
    if (cmd == NULL)
    {
        return;
    }

    uint8_t address[6];
    hmi_phone_state_t old;
    os_mutex_take(phone_lock, 0xFFFFFFFFu);
    old = phone_state;
    memcpy(address, phone_addr, sizeof(address));
    os_mutex_give(phone_lock);

    if (old != HMI_PHONE_DISCONNECTED)
    {
        switch (cmd->op)
        {
        case PHONE_DIAL:
            if (old == HMI_PHONE_IDLE && bt_hfp_dial_with_number_req(address, cmd->number))
            {
                os_mutex_take(phone_lock, 0xFFFFFFFFu);
                hmi_phone_state_t previous = phone_state;
                phone_state = HMI_PHONE_DIALING;
                bool changed = phone_update_number(cmd->number, sizeof(cmd->number));
                hmi_phone_state_t current = phone_state;
                os_mutex_give(phone_lock);
                phone_publish(previous, current, changed);
            }
            break;
        case PHONE_ANSWER:
            (void)bt_hfp_call_answer_req(address);
            break;
        case PHONE_HANGUP:
            (void)bt_hfp_call_terminate_req(address);
            break;
        case PHONE_MUTE:
            os_mutex_take(phone_lock, 0xFFFFFFFFu);
            phone_muted = cmd->value;
            os_mutex_give(phone_lock);
            phone_publish_bool(EVT_PHONE_MUTED_CHANGED, cmd->value);
            break;
        case PHONE_ROUTE:
            if (old == HMI_PHONE_ACTIVE)
            {
                bool ok = cmd->value ? bt_hfp_audio_connect_req(address) :
                                       bt_hfp_audio_disconnect_req(address);
                if (ok)
                {
                    phone_watch_audio = cmd->value;
                    phone_publish_bool(EVT_PHONE_ROUTE_CHANGED, cmd->value);
                }
            }
            break;
        default:
            break;
        }
    }
    free(cmd);
}

static void phone_audio_cback(T_AUDIO_EVENT event, void *event_buf, uint16_t length)
{
    T_AUDIO_EVENT_PARAM *param = event_buf;
    if (param == NULL || phone_lock == NULL ||
        event != AUDIO_EVENT_TRACK_DATA_IND ||
        length < sizeof(T_AUDIO_EVENT_PARAM_TRACK_DATA_IND))
    {
        return;
    }

    os_mutex_take(phone_lock, 0xFFFFFFFFu);
    T_AUDIO_TRACK_HANDLE track = voice_track;
    bool muted = phone_muted;
    uint8_t address[6];
    memcpy(address, phone_addr, sizeof(address));
    bool match = track != NULL && param->track_data_ind.handle == track;
    if (match)
    {
        audio_busy++;
    }
    os_mutex_give(phone_lock);
    if (!match)
    {
        return;
    }

    uint16_t len = param->track_data_ind.len;
    uint8_t *data = len != 0 ? malloc(len) : NULL;
    if (data != NULL)
    {
        uint32_t timestamp;
        uint16_t sequence;
        uint16_t read_len;
        uint8_t frames;
        T_AUDIO_STREAM_STATUS status;
        if (audio_track_read(track, &timestamp, &sequence, &status,
                             &frames, data, len, &read_len) && read_len != 0)
        {
            if (muted)
            {
                memset(data, 0, read_len);
            }
            (void)bt_sco_data_send(address, sequence, data, read_len);
        }
        free(data);
    }

    os_mutex_take(phone_lock, 0xFFFFFFFFu);
    audio_busy--;
    T_AUDIO_TRACK_HANDLE release[HMI_HFP_RETIRED_MAX];
    unsigned release_count = 0;
    if (audio_busy == 0)
    {
        release_count = retired_count;
        memcpy(release, retired_tracks, release_count * sizeof(release[0]));
        retired_count = 0;
    }
    os_mutex_give(phone_lock);
    for (unsigned i = 0; i < release_count; ++i)
    {
        audio_track_release(release[i]);
    }
}

static void voice_track_release(void)
{
    T_AUDIO_TRACK_HANDLE release[HMI_HFP_RETIRED_MAX + 1u];
    unsigned release_count = 0;
    bool overflow = false;

    os_mutex_take(phone_lock, 0xFFFFFFFFu);
    T_AUDIO_TRACK_HANDLE current = voice_track;
    voice_track = NULL;
    if (current != NULL)
    {
        if (audio_busy == 0)
        {
            release[release_count++] = current;
        }
        else if (retired_count < HMI_HFP_RETIRED_MAX)
        {
            retired_tracks[retired_count++] = current;
        }
        else
        {
            overflow = true;
        }
    }
    if (audio_busy == 0 && retired_count != 0)
    {
        memcpy(&release[release_count], retired_tracks,
               retired_count * sizeof(release[0]));
        release_count += retired_count;
        retired_count = 0;
    }
    os_mutex_give(phone_lock);
    if (overflow)
    {
        APP_PRINT_ERROR0("hmi_hfp: retired voice track overflow");
    }
    for (unsigned i = 0; i < release_count; ++i)
    {
        audio_track_release(release[i]);
    }
}

static const uint8_t hmi_hfp_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x4B,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_HANDSFREE >> 8),
    (uint8_t)(UUID_HANDSFREE),
    SDP_UUID16_HDR,
    (uint8_t)(UUID_GENERIC_AUDIO >> 8),
    (uint8_t)(UUID_GENERIC_AUDIO),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROTO_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROTO_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x0C,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_L2CAP >> 8),
    (uint8_t)(UUID_L2CAP),
    SDP_DATA_ELEM_SEQ_HDR,
    0x05,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_RFCOMM >> 8),
    (uint8_t)(UUID_RFCOMM),
    SDP_UNSIGNED_ONE_BYTE,
    HMI_HFP_RFC_CHANN_NUM,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)UUID_PUBLIC_BROWSE_GROUP,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_HANDSFREE >> 8),
    (uint8_t)(UUID_HANDSFREE),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0109 >> 8),
    (uint8_t)(0x0109),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)((SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET) >> 8),
    (uint8_t)(SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET),
    SDP_STRING_HDR,
    0x0F,
    'H', 'a', 'n', 'd', 's', '-', 'F', 'r', 'e', 'e', ' ', 'u', 'n', 'i', 't',
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES >> 8),
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x013F >> 8),
    (uint8_t)(0x013F)
};

static void hmi_hfp_bt_cback(T_BT_EVENT event_type, void *event_buf, uint16_t buf_len)
{
    T_BT_EVENT_PARAM *param = event_buf;
    T_BR_EDR_LINK    *p_link;
    T_HMI_HFP_LINK   *p_hfp;

    switch (event_type)
    {
    /* ---- HF role events ---- */
    case BT_EVENT_HFP_CONN_IND:
        p_link = hmi_br_edr_find_link(param->hfp_conn_ind.bd_addr);
        if (p_link != NULL)
        {
            bt_hfp_connect_cfm(p_link->bd_addr, true);
        }
        break;

    case BT_EVENT_HFP_CONN_CMPL:
        p_link = hmi_br_edr_find_link(param->hfp_conn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            hfp_link[p_link->id].hf_connected = true;
            os_mutex_take(phone_lock, 0xFFFFFFFFu);
            hmi_phone_state_t previous = phone_state;
            memcpy(phone_addr, p_link->bd_addr, sizeof(phone_addr));
            phone_state = HMI_PHONE_IDLE;
            bool changed = phone_update_number("", 0);
            hmi_phone_state_t current = phone_state;
            os_mutex_give(phone_lock);
            phone_publish(previous, current, changed);
            APP_PRINT_INFO0("hmi_hfp: HF connected");
        }
        break;

    case BT_EVENT_HFP_DISCONN_CMPL:
        p_link = hmi_br_edr_find_link(param->hfp_disconn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            hfp_link[p_link->id].hf_connected = false;
            voice_track_release();
            os_mutex_take(phone_lock, 0xFFFFFFFFu);
            hmi_phone_state_t previous = phone_state;
            phone_state = HMI_PHONE_DISCONNECTED;
            phone_muted = false;
            phone_watch_audio = true;
            memset(phone_addr, 0, sizeof(phone_addr));
            bool changed = phone_update_number("", 0);
            hmi_phone_state_t current = phone_state;
            os_mutex_give(phone_lock);
            phone_publish(previous, current, changed);
            phone_publish_bool(EVT_PHONE_MUTED_CHANGED, false);
            phone_publish_bool(EVT_PHONE_ROUTE_CHANGED, true);
            APP_PRINT_INFO0("hmi_hfp: HF disconnected");
        }
        break;

    case BT_EVENT_HFP_CALL_STATUS:
        p_link = hmi_br_edr_find_link(param->hfp_call_status.bd_addr);
        if (p_link != NULL && hfp_link[p_link->id].hf_connected)
        {
            os_mutex_take(phone_lock, 0xFFFFFFFFu);
            hmi_phone_state_t previous = phone_state;
            bool changed = false;
            bool muted_changed = false;
            switch (param->hfp_call_status.curr_status)
            {
            case BT_HFP_CALL_IDLE:
                phone_state = HMI_PHONE_IDLE;
                muted_changed = phone_muted;
                phone_muted = false;
                changed = phone_update_number("", 0);
                break;
            case BT_HFP_CALL_INCOMING: phone_state = HMI_PHONE_INCOMING; break;
            case BT_HFP_CALL_OUTGOING: phone_state = HMI_PHONE_DIALING; break;
            case BT_HFP_CALL_ACTIVE: phone_state = HMI_PHONE_ACTIVE; break;
            case BT_HFP_CALL_HELD: phone_state = HMI_PHONE_ACTIVE; break;
            default: break;
            }
            hmi_phone_state_t current = phone_state;
            os_mutex_give(phone_lock);
            phone_publish(previous, current, changed);
            if (muted_changed)
            {
                phone_publish_bool(EVT_PHONE_MUTED_CHANGED, false);
            }
            if (previous != HMI_PHONE_ACTIVE && current == HMI_PHONE_ACTIVE &&
                !hfp_link[p_link->id].sco_connected && phone_watch_audio)
            {
                (void)bt_hfp_audio_connect_req(p_link->bd_addr);
            }
            if (hfp_call_status_cb != NULL)
            {
                hfp_call_status_cb(param->hfp_call_status.bd_addr,
                                   param->hfp_call_status.curr_status);
            }
        }
        break;

    case BT_EVENT_HFP_CALLER_ID_IND:
    case BT_EVENT_HFP_CURRENT_CALL_LIST_IND:
    case BT_EVENT_HFP_CALL_WAITING_IND:
        {
            const char *number = event_type == BT_EVENT_HFP_CALLER_ID_IND ?
                param->hfp_caller_id_ind.number :
                event_type == BT_EVENT_HFP_CURRENT_CALL_LIST_IND ?
                param->hfp_current_call_list_ind.number : param->hfp_call_waiting_ind.number;
            const uint8_t *addr = event_type == BT_EVENT_HFP_CALLER_ID_IND ?
                param->hfp_caller_id_ind.bd_addr :
                event_type == BT_EVENT_HFP_CURRENT_CALL_LIST_IND ?
                param->hfp_current_call_list_ind.bd_addr : param->hfp_call_waiting_ind.bd_addr;
            p_link = hmi_br_edr_find_link((uint8_t *)addr);
            if (p_link != NULL && hfp_link[p_link->id].hf_connected && number[0] != '\0')
            {
                os_mutex_take(phone_lock, 0xFFFFFFFFu);
                bool changed = phone_update_number(number, 20u);
                hmi_phone_state_t current = phone_state;
                os_mutex_give(phone_lock);
                phone_publish(current, current, changed);
            }
        }
        break;

    /* ---- SCO events ---- */
    case BT_EVENT_SCO_CONN_IND:
        p_link = hmi_br_edr_find_link(param->sco_conn_ind.bd_addr);
        if (p_link != NULL)
        {
            p_hfp = &hfp_link[p_link->id];
            if (p_hfp->hf_connected)
            {
                bt_hfp_audio_connect_cfm(p_link->bd_addr, true);
            }
        }
        break;

    case BT_EVENT_SCO_CONN_CMPL:
        p_link = hmi_br_edr_find_link(param->sco_conn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            p_hfp             = &hfp_link[p_link->id];
            p_hfp->sco_connected = true;
            p_hfp->air_mode      = param->sco_conn_cmpl.air_mode;
            voice_seq = 0;
            T_AUDIO_FORMAT_INFO format = {0};
            if (p_hfp->air_mode == 3u)
            {
                format.type = AUDIO_FORMAT_TYPE_MSBC;
                format.frame_num = 1;
                format.attr.msbc.chann_mode = AUDIO_SBC_CHANNEL_MODE_MONO;
                format.attr.msbc.sample_rate = 16000;
                format.attr.msbc.bitpool = 26;
                format.attr.msbc.allocation_method = 0;
                format.attr.msbc.subband_num = 8;
                format.attr.msbc.block_length = 15;
                format.attr.msbc.chann_location = AUDIO_CHANNEL_LOCATION_MONO;
            }
            else if (p_hfp->air_mode == 2u)
            {
                format.type = AUDIO_FORMAT_TYPE_CVSD;
                format.frame_num = 1;
                format.attr.cvsd.chann_num = 1;
                format.attr.cvsd.sample_rate = 8000;
                format.attr.cvsd.chann_location = AUDIO_CHANNEL_LOCATION_MONO;
                format.attr.cvsd.frame_duration = param->sco_conn_cmpl.rx_pkt_len == 30u ?
                    AUDIO_CVSD_FRAME_DURATION_3_75_MS : AUDIO_CVSD_FRAME_DURATION_7_5_MS;
            }
            if (p_hfp->air_mode == 2u || p_hfp->air_mode == 3u)
            {
                voice_track_release();
                T_AUDIO_TRACK_HANDLE track = audio_track_create(
                    AUDIO_STREAM_TYPE_VOICE, AUDIO_STREAM_MODE_NORMAL,
                    AUDIO_STREAM_USAGE_SNOOP, format, 10, 8,
                    AUDIO_DEVICE_OUT_SPK | AUDIO_DEVICE_IN_MIC, NULL, NULL);
                if (track != NULL)
                {
                    (void)audio_track_latency_set(track, 15, false);
                    if (audio_track_start(track))
                    {
                        os_mutex_take(phone_lock, 0xFFFFFFFFu);
                        voice_track = track;
                        os_mutex_give(phone_lock);
                    }
                    else
                    {
                        audio_track_release(track);
                    }
                }
            }
            APP_PRINT_INFO1("hmi_hfp: SCO connected, air_mode %d", p_hfp->air_mode);
            if (hfp_sco_conn_cb != NULL)
            {
                hfp_sco_conn_cb(param->sco_conn_cmpl.bd_addr, p_hfp->air_mode);
            }
        }
        break;

    case BT_EVENT_SCO_DATA_IND:
        p_link = hmi_br_edr_find_link(param->sco_data_ind.bd_addr);
        if (p_link != NULL)
        {
            T_AUDIO_TRACK_HANDLE track = voice_track;
            if (track != NULL)
            {
                uint16_t written = 0;
                T_AUDIO_STREAM_STATUS status = param->sco_data_ind.status == BT_SCO_PKT_STATUS_OK ?
                    AUDIO_STREAM_STATUS_CORRECT : AUDIO_STREAM_STATUS_LOST;
                (void)audio_track_write(track, param->sco_data_ind.bt_clk, ++voice_seq,
                                        status, 1, param->sco_data_ind.p_data,
                                        param->sco_data_ind.length, &written);
            }
            if (hfp_sco_data_cb != NULL)
            {
                hfp_sco_data_cb(param->sco_data_ind.bd_addr,
                                param->sco_data_ind.p_data,
                                param->sco_data_ind.length);
            }
        }
        break;

    case BT_EVENT_SCO_DISCONNECTED:
        p_link = hmi_br_edr_find_link(param->sco_disconnected.bd_addr);
        if (p_link != NULL)
        {
            voice_track_release();
            hfp_link[p_link->id].sco_connected = false;
            hfp_link[p_link->id].air_mode      = 0;
            APP_PRINT_INFO0("hmi_hfp: SCO disconnected");
            if (hfp_sco_disconn_cb != NULL)
            {
                hfp_sco_disconn_cb(param->sco_disconnected.bd_addr);
            }
        }
        break;

    default:
        break;
    }
}

void hmi_bt_hfp_set_call_status_cb(void (*cb)(uint8_t *bd_addr, uint8_t status))
{
    hfp_call_status_cb = cb;
}

void hmi_bt_hfp_set_sco_conn_cb(void (*cb)(uint8_t *bd_addr, uint8_t air_mode))
{
    hfp_sco_conn_cb = cb;
}

void hmi_bt_hfp_set_sco_data_cb(void (*cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len))
{
    hfp_sco_data_cb = cb;
}

void hmi_bt_hfp_set_sco_disconn_cb(void (*cb)(uint8_t *bd_addr))
{
    hfp_sco_disconn_cb = cb;
}

bool hmi_bt_hfp_sco_send(uint8_t *bd_addr, uint16_t seq_num, uint8_t *buf, uint16_t len)
{
    T_BR_EDR_LINK *p_link = hmi_br_edr_find_link(bd_addr);

    if (p_link == NULL || !hfp_link[p_link->id].sco_connected)
    {
        return false;
    }

    return bt_sco_data_send(bd_addr, seq_num, buf, len);
}

void hmi_bt_hfp_init(void)
{
    memset(hfp_link, 0, sizeof(hfp_link));
    if (!os_mutex_create(&phone_lock))
    {
        APP_PRINT_ERROR0("hmi_hfp: phone mutex create failed");
        return;
    }
    phone_state = HMI_PHONE_DISCONNECTED;
    (void)audio_mgr_cback_register(phone_audio_cback);

    bt_sdp_record_add((void *)hmi_hfp_sdp_record);

    bt_hfp_init(HMI_HFP_RFC_CHANN_NUM, HMI_HSP_RFC_CHANN_NUM,
                BT_HFP_HF_LOCAL_THREE_WAY_CALLING           |
                BT_HFP_HF_LOCAL_CLI_PRESENTATION_CAPABILITY |
                BT_HFP_HF_LOCAL_VOICE_RECOGNITION_ACTIVATION |
                BT_HFP_HF_LOCAL_CODEC_NEGOTIATION           |
                BT_HFP_HF_LOCAL_ESCO_S4_SETTINGS            |
                BT_HFP_HF_LOCAL_REMOTE_VOLUME_CONTROL,
                BT_HFP_HF_CODEC_TYPE_CVSD | BT_HFP_HF_CODEC_TYPE_MSBC);

    bt_mgr_cback_register(hmi_hfp_bt_cback);
}
