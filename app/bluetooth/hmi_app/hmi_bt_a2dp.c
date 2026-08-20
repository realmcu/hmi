/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#include <string.h>
#include "trace.h"
#include "bt_types.h"
#include "btm.h"
#include "bt_sdp.h"
#include "bt_a2dp.h"
#include "hmi_br_edr_link.h"
#include "hmi_bt_a2dp.h"

#define HMI_A2DP_SRC_MAX_CREDITS  8

typedef struct
{
    bool               connected;
    uint8_t            role;
    bool               streaming;
    uint8_t            src_credits;
    T_HMI_A2DP_CODEC_SBC codec;
} T_HMI_A2DP_LINK;

static T_HMI_A2DP_LINK a2dp_link[MAX_BR_LINK_NUM];

static void (*a2dp_config_cb)(uint8_t *bd_addr, uint8_t role, T_HMI_A2DP_CODEC_SBC *codec);
static void (*a2dp_snk_data_cb)(uint8_t *bd_addr, uint8_t frame_num, uint8_t *payload,
                                uint16_t len);
static void (*a2dp_stream_start_cb)(uint8_t *bd_addr, uint8_t role);
static void (*a2dp_stream_stop_cb)(uint8_t *bd_addr);

static const uint8_t hmi_a2dp_sink_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x39,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AUDIO_SINK >> 8),
    (uint8_t)(UUID_AUDIO_SINK),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROTO_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROTO_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x10,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_L2CAP >> 8),
    (uint8_t)(UUID_L2CAP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(PSM_AVDTP >> 8),
    (uint8_t)(PSM_AVDTP),
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AVDTP >> 8),
    (uint8_t)(UUID_AVDTP),
    SDP_UNSIGNED_TWO_BYTE,
    0x01,
    0x03,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_ADVANCED_AUDIO_DISTRIBUTION >> 8),
    (uint8_t)(UUID_ADVANCED_AUDIO_DISTRIBUTION),
    SDP_UNSIGNED_TWO_BYTE,
    0x01,
    0x03,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES >> 8),
    (uint8_t)SDP_ATTR_SUPPORTED_FEATURES,
    SDP_UNSIGNED_TWO_BYTE,
    0x00,
    0x03
};

static const uint8_t hmi_a2dp_src_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x39,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AUDIO_SOURCE >> 8),
    (uint8_t)(UUID_AUDIO_SOURCE),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROTO_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROTO_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x10,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_L2CAP >> 8),
    (uint8_t)(UUID_L2CAP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(PSM_AVDTP >> 8),
    (uint8_t)(PSM_AVDTP),
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AVDTP >> 8),
    (uint8_t)(UUID_AVDTP),
    SDP_UNSIGNED_TWO_BYTE,
    0x01,
    0x03,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_ADVANCED_AUDIO_DISTRIBUTION >> 8),
    (uint8_t)(UUID_ADVANCED_AUDIO_DISTRIBUTION),
    SDP_UNSIGNED_TWO_BYTE,
    0x01,
    0x03,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES >> 8),
    (uint8_t)SDP_ATTR_SUPPORTED_FEATURES,
    SDP_UNSIGNED_TWO_BYTE,
    0x00,
    0x03
};

static void hmi_a2dp_bt_cback(T_BT_EVENT event_type, void *event_buf, uint16_t buf_len)
{
    T_BT_EVENT_PARAM *param = event_buf;
    T_BR_EDR_LINK    *p_link;
    T_HMI_A2DP_LINK  *p_a2dp;

    switch (event_type)
    {
    case BT_EVENT_A2DP_CONN_IND:
        p_link = hmi_br_edr_find_link(param->a2dp_conn_ind.bd_addr);
        if (p_link != NULL)
        {
            bt_a2dp_connect_cfm(p_link->bd_addr, 0, true);
        }
        break;

    case BT_EVENT_A2DP_CONN_CMPL:
        p_link = hmi_br_edr_find_link(param->a2dp_conn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            p_a2dp              = &a2dp_link[p_link->id];
            p_a2dp->connected   = true;
            p_a2dp->src_credits = HMI_A2DP_SRC_MAX_CREDITS;
            APP_PRINT_INFO0("hmi_a2dp: connected");
        }
        break;

    case BT_EVENT_A2DP_DISCONN_CMPL:
        p_link = hmi_br_edr_find_link(param->a2dp_disconn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            memset(&a2dp_link[p_link->id], 0, sizeof(T_HMI_A2DP_LINK));
            APP_PRINT_INFO0("hmi_a2dp: disconnected");
        }
        break;

    case BT_EVENT_A2DP_CONFIG_CMPL:
        p_link = hmi_br_edr_find_link(param->a2dp_config_cmpl.bd_addr);
        if (p_link != NULL)
        {
            p_a2dp = &a2dp_link[p_link->id];

            if (param->a2dp_config_cmpl.role == BT_A2DP_ROLE_SNK)
            {
                bt_a2dp_stream_delay_report_req(param->a2dp_config_cmpl.bd_addr, 280);
            }

            if (param->a2dp_config_cmpl.codec_type == BT_A2DP_CODEC_TYPE_SBC)
            {
                p_a2dp->codec.sampling_frequency =
                    param->a2dp_config_cmpl.codec_info.sbc.sampling_frequency;
                p_a2dp->codec.channel_mode =
                    param->a2dp_config_cmpl.codec_info.sbc.channel_mode;
                p_a2dp->codec.block_length =
                    param->a2dp_config_cmpl.codec_info.sbc.block_length;
                p_a2dp->codec.subbands =
                    param->a2dp_config_cmpl.codec_info.sbc.subbands;
                p_a2dp->codec.allocation_method =
                    param->a2dp_config_cmpl.codec_info.sbc.allocation_method;

                if (a2dp_config_cb != NULL)
                {
                    a2dp_config_cb(param->a2dp_config_cmpl.bd_addr,
                                   param->a2dp_config_cmpl.role,
                                   &p_a2dp->codec);
                }
            }
        }
        break;

    case BT_EVENT_A2DP_STREAM_OPEN:
        break;

    case BT_EVENT_A2DP_STREAM_START_IND:
        p_link = hmi_br_edr_find_link(param->a2dp_stream_start_ind.bd_addr);
        if (p_link != NULL)
        {
            p_a2dp = &a2dp_link[p_link->id];
            if (p_a2dp->role == BT_A2DP_ROLE_SNK)
            {
                bt_a2dp_stream_start_cfm(param->a2dp_stream_start_ind.bd_addr, true);
            }
            p_a2dp->streaming = true;
            if (a2dp_stream_start_cb != NULL)
            {
                a2dp_stream_start_cb(param->a2dp_stream_start_ind.bd_addr, p_a2dp->role);
            }
        }
        break;

    case BT_EVENT_A2DP_STREAM_START_RSP:
        p_link = hmi_br_edr_find_link(param->a2dp_stream_start_rsp.bd_addr);
        if (p_link != NULL)
        {
            p_a2dp = &a2dp_link[p_link->id];
            p_a2dp->streaming = true;
            if (a2dp_stream_start_cb != NULL)
            {
                a2dp_stream_start_cb(param->a2dp_stream_start_rsp.bd_addr, p_a2dp->role);
            }
        }
        break;

    case BT_EVENT_A2DP_STREAM_DATA_IND:
        p_link = hmi_br_edr_find_link(param->a2dp_stream_data_ind.bd_addr);
        if (p_link != NULL)
        {
            p_a2dp = &a2dp_link[p_link->id];
            if (p_a2dp->role == BT_A2DP_ROLE_SNK && a2dp_snk_data_cb != NULL)
            {
                a2dp_snk_data_cb(param->a2dp_stream_data_ind.bd_addr,
                                 param->a2dp_stream_data_ind.frame_num,
                                 param->a2dp_stream_data_ind.payload,
                                 param->a2dp_stream_data_ind.len);
            }
        }
        break;

    case BT_EVENT_A2DP_STREAM_DATA_RSP:
        p_link = hmi_br_edr_find_link(param->a2dp_stream_data_rsp.bd_addr);
        if (p_link != NULL)
        {
            p_a2dp = &a2dp_link[p_link->id];
            if (p_a2dp->src_credits < HMI_A2DP_SRC_MAX_CREDITS)
            {
                p_a2dp->src_credits++;
            }
        }
        break;

    case BT_EVENT_A2DP_STREAM_STOP:
        p_link = hmi_br_edr_find_link(param->a2dp_stream_stop.bd_addr);
        if (p_link != NULL)
        {
            a2dp_link[p_link->id].streaming = false;
            if (a2dp_stream_stop_cb != NULL)
            {
                a2dp_stream_stop_cb(param->a2dp_stream_stop.bd_addr);
            }
        }
        break;

    case BT_EVENT_A2DP_STREAM_CLOSE:
        p_link = hmi_br_edr_find_link(param->a2dp_stream_close.bd_addr);
        if (p_link != NULL)
        {
            a2dp_link[p_link->id].streaming = false;
            if (a2dp_stream_stop_cb != NULL)
            {
                a2dp_stream_stop_cb(param->a2dp_stream_close.bd_addr);
            }
        }
        break;

    default:
        break;
    }
}

void hmi_bt_a2dp_set_config_cb(
    void (*cb)(uint8_t *bd_addr, uint8_t role, T_HMI_A2DP_CODEC_SBC *codec))
{
    a2dp_config_cb = cb;
}

void hmi_bt_a2dp_set_snk_data_cb(
    void (*cb)(uint8_t *bd_addr, uint8_t frame_num, uint8_t *payload, uint16_t len))
{
    a2dp_snk_data_cb = cb;
}

void hmi_bt_a2dp_set_stream_start_cb(void (*cb)(uint8_t *bd_addr, uint8_t role))
{
    a2dp_stream_start_cb = cb;
}

void hmi_bt_a2dp_set_stream_stop_cb(void (*cb)(uint8_t *bd_addr))
{
    a2dp_stream_stop_cb = cb;
}

bool hmi_bt_a2dp_src_send(uint8_t *bd_addr, uint16_t seq_num, uint32_t timestamp,
                          uint8_t frames_per_pkt, uint8_t *payload, uint16_t len)
{
    T_BR_EDR_LINK   *p_link = hmi_br_edr_find_link(bd_addr);
    T_HMI_A2DP_LINK *p_a2dp;

    if (p_link == NULL)
    {
        return false;
    }

    p_a2dp = &a2dp_link[p_link->id];
    if (!p_a2dp->connected || !p_a2dp->streaming || p_a2dp->src_credits == 0)
    {
        return false;
    }

    if (bt_a2dp_stream_data_send(bd_addr, seq_num, timestamp, frames_per_pkt, payload, len, false))
    {
        p_a2dp->src_credits--;
        return true;
    }

    return false;
}

void hmi_bt_a2dp_init(void)
{
    T_BT_A2DP_STREAM_ENDPOINT sep;

    memset(a2dp_link, 0, sizeof(a2dp_link));

    bt_sdp_record_add((void *)hmi_a2dp_sink_sdp_record);
    bt_sdp_record_add((void *)hmi_a2dp_src_sdp_record);

    bt_a2dp_init(BT_A2DP_CAPABILITY_MEDIA_TRANSPORT |
                 BT_A2DP_CAPABILITY_MEDIA_CODEC      |
                 BT_A2DP_CAPABILITY_DELAY_REPORTING);

    /* SNK SEP: 44.1KHz + 48KHz, all channel modes and block lengths */
    sep.role        = BT_A2DP_ROLE_SNK;
    sep.codec_type  = BT_A2DP_CODEC_TYPE_SBC;
    sep.u.codec_sbc.sampling_frequency_mask = BT_A2DP_SBC_SAMPLING_FREQUENCY_44_1KHZ |
                                              BT_A2DP_SBC_SAMPLING_FREQUENCY_48KHZ;
    sep.u.codec_sbc.channel_mode_mask       = BT_A2DP_SBC_CHANNEL_MODE_MONO         |
                                              BT_A2DP_SBC_CHANNEL_MODE_DUAL_CHANNEL  |
                                              BT_A2DP_SBC_CHANNEL_MODE_STEREO        |
                                              BT_A2DP_SBC_CHANNEL_MODE_JOINT_STEREO;
    sep.u.codec_sbc.block_length_mask       = BT_A2DP_SBC_BLOCK_LENGTH_4  |
                                              BT_A2DP_SBC_BLOCK_LENGTH_8  |
                                              BT_A2DP_SBC_BLOCK_LENGTH_12 |
                                              BT_A2DP_SBC_BLOCK_LENGTH_16;
    sep.u.codec_sbc.subbands_mask           = BT_A2DP_SBC_SUBBANDS_4 |
                                              BT_A2DP_SBC_SUBBANDS_8;
    sep.u.codec_sbc.allocation_method_mask  = BT_A2DP_SBC_ALLOCATION_METHOD_SNR      |
                                              BT_A2DP_SBC_ALLOCATION_METHOD_LOUDNESS;
    sep.u.codec_sbc.min_bitpool             = 0x02;
    sep.u.codec_sbc.max_bitpool             = 0x23;
    bt_a2dp_stream_endpoint_add(sep);

    /* SRC SEP: 48KHz, Joint Stereo, Block16, Sub8, Loudness */
    sep.role        = BT_A2DP_ROLE_SRC;
    sep.codec_type  = BT_A2DP_CODEC_TYPE_SBC;
    sep.u.codec_sbc.sampling_frequency_mask = BT_A2DP_SBC_SAMPLING_FREQUENCY_48KHZ;
    sep.u.codec_sbc.channel_mode_mask       = BT_A2DP_SBC_CHANNEL_MODE_JOINT_STEREO;
    sep.u.codec_sbc.block_length_mask       = BT_A2DP_SBC_BLOCK_LENGTH_16;
    sep.u.codec_sbc.subbands_mask           = BT_A2DP_SBC_SUBBANDS_8;
    sep.u.codec_sbc.allocation_method_mask  = BT_A2DP_SBC_ALLOCATION_METHOD_LOUDNESS;
    sep.u.codec_sbc.min_bitpool             = 0x02;
    sep.u.codec_sbc.max_bitpool             = 0x23;
    bt_a2dp_stream_endpoint_add(sep);

    bt_mgr_cback_register(hmi_a2dp_bt_cback);
}
