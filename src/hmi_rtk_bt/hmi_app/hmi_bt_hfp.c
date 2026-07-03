/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include "trace.h"
#include "bt_types.h"
#include "btm.h"
#include "bt_sdp.h"
#include "bt_hfp.h"
#include "bt_hfp_ag.h"
#include "hmi_br_edr_link.h"
#include "hmi_bt_hfp.h"

#define HMI_HFP_RFC_CHANN_NUM        1
#define HMI_HSP_RFC_CHANN_NUM        2
#define HMI_HSP_AG_RFC_CHANN_NUM     22
#define HMI_HFP_AG_RFC_CHANN_NUM     23

typedef struct
{
    bool    hf_connected;
    bool    ag_connected;
    bool    sco_connected;
    uint8_t air_mode;       /* 2=CVSD, 3=mSBC */
} T_HMI_HFP_LINK;

static T_HMI_HFP_LINK hfp_link[MAX_BR_LINK_NUM];

static void (*hfp_call_status_cb)(uint8_t *bd_addr, uint8_t status);
static void (*hfp_sco_conn_cb)(uint8_t *bd_addr, uint8_t air_mode);
static void (*hfp_sco_data_cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len);
static void (*hfp_sco_disconn_cb)(uint8_t *bd_addr);

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

static const uint8_t hmi_hfp_ag_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x34,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_HANDSFREE_AUDIO_GATEWAY >> 8),
    (uint8_t)(UUID_HANDSFREE_AUDIO_GATEWAY),
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
    HMI_HFP_AG_RFC_CHANN_NUM,
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
    (uint8_t)(SDP_ATTR_EXT_NETWORK >> 8),
    (uint8_t)(SDP_ATTR_EXT_NETWORK),
    SDP_UNSIGNED_ONE_BYTE,
    (uint8_t)(0x01),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES >> 8),
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x012F >> 8),
    (uint8_t)(0x012F)
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
            APP_PRINT_INFO0("hmi_hfp: HF connected");
        }
        break;

    case BT_EVENT_HFP_DISCONN_CMPL:
        p_link = hmi_br_edr_find_link(param->hfp_disconn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            hfp_link[p_link->id].hf_connected = false;
            APP_PRINT_INFO0("hmi_hfp: HF disconnected");
        }
        break;

    case BT_EVENT_HFP_CALL_STATUS:
        p_link = hmi_br_edr_find_link(param->hfp_call_status.bd_addr);
        if (p_link != NULL && hfp_call_status_cb != NULL)
        {
            hfp_call_status_cb(param->hfp_call_status.bd_addr,
                               param->hfp_call_status.curr_status);
        }
        break;

    /* ---- AG role events ---- */
    case BT_EVENT_HFP_AG_CONN_IND:
        p_link = hmi_br_edr_find_link(param->hfp_ag_conn_ind.bd_addr);
        if (p_link != NULL)
        {
            bt_hfp_ag_connect_cfm(p_link->bd_addr, true);
        }
        break;

    case BT_EVENT_HFP_AG_CONN_CMPL:
        p_link = hmi_br_edr_find_link(param->hfp_ag_conn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            hfp_link[p_link->id].ag_connected = true;
            APP_PRINT_INFO0("hmi_hfp: AG connected");
        }
        break;

    case BT_EVENT_HFP_AG_DISCONN_CMPL:
        p_link = hmi_br_edr_find_link(param->hfp_ag_disconn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            hfp_link[p_link->id].ag_connected = false;
            APP_PRINT_INFO0("hmi_hfp: AG disconnected");
        }
        break;

    case BT_EVENT_HFP_AG_INDICATORS_STATUS_REQ:
        p_link = hmi_br_edr_find_link(param->hfp_ag_indicators_status_req.bd_addr);
        if (p_link != NULL)
        {
            bt_hfp_ag_indicators_send(p_link->bd_addr,
                                      BT_HFP_AG_SERVICE_STATUS_AVAILABLE,
                                      BT_HFP_AG_NO_CALL_IN_PROGRESS,
                                      BT_HFP_AG_CALL_SETUP_STATUS_IDLE,
                                      BT_HFP_AG_CALL_HELD_STATUS_IDLE,
                                      5,
                                      BT_HFP_AG_ROAMING_STATUS_ACTIVE,
                                      5);
            bt_hfp_ag_ok_send(p_link->bd_addr);
        }
        break;

    case BT_EVENT_HFP_AG_CURR_CALLS_LIST_QUERY:
        p_link = hmi_br_edr_find_link(param->hfp_ag_curr_calls_list_query.bd_addr);
        if (p_link != NULL)
        {
            bt_hfp_ag_ok_send(p_link->bd_addr);
        }
        break;

    case BT_EVENT_HFP_AG_CALL_ANSWER_REQ:
        bt_hfp_ag_call_answer(param->hfp_ag_call_answer_req.bd_addr);
        break;

    case BT_EVENT_HFP_AG_CALL_TERMINATE_REQ:
        bt_hfp_ag_call_terminate(param->hfp_ag_call_terminate_req.bd_addr);
        break;

    case BT_EVENT_HFP_AG_CALL_STATUS_CHANGED:
        if (param->hfp_ag_call_status_changed.curr_status == BT_HFP_AG_CALL_IDLE)
        {
            bt_hfp_ag_audio_disconnect_req(param->hfp_ag_call_status_changed.bd_addr);
        }
        break;

    /* ---- SCO events (shared by HF and AG) ---- */
    case BT_EVENT_SCO_CONN_IND:
        p_link = hmi_br_edr_find_link(param->sco_conn_ind.bd_addr);
        if (p_link != NULL)
        {
            p_hfp = &hfp_link[p_link->id];
            if (p_hfp->ag_connected)
            {
                bt_hfp_ag_audio_connect_cfm(p_link->bd_addr, true);
            }
            else if (p_hfp->hf_connected)
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
            APP_PRINT_INFO1("hmi_hfp: SCO connected, air_mode %d", p_hfp->air_mode);
            if (hfp_sco_conn_cb != NULL)
            {
                hfp_sco_conn_cb(param->sco_conn_cmpl.bd_addr, p_hfp->air_mode);
            }
        }
        break;

    case BT_EVENT_SCO_DATA_IND:
        p_link = hmi_br_edr_find_link(param->sco_data_ind.bd_addr);
        if (p_link != NULL && hfp_sco_data_cb != NULL)
        {
            hfp_sco_data_cb(param->sco_data_ind.bd_addr,
                            param->sco_data_ind.p_data,
                            param->sco_data_ind.length);
        }
        break;

    case BT_EVENT_SCO_DISCONNECTED:
        p_link = hmi_br_edr_find_link(param->sco_disconnected.bd_addr);
        if (p_link != NULL)
        {
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

    bt_sdp_record_add((void *)hmi_hfp_sdp_record);
    bt_sdp_record_add((void *)hmi_hfp_ag_sdp_record);

    bt_hfp_init(HMI_HFP_RFC_CHANN_NUM, HMI_HSP_RFC_CHANN_NUM,
                BT_HFP_HF_LOCAL_THREE_WAY_CALLING           |
                BT_HFP_HF_LOCAL_CLI_PRESENTATION_CAPABILITY |
                BT_HFP_HF_LOCAL_VOICE_RECOGNITION_ACTIVATION |
                BT_HFP_HF_LOCAL_CODEC_NEGOTIATION           |
                BT_HFP_HF_LOCAL_ESCO_S4_SETTINGS            |
                BT_HFP_HF_LOCAL_REMOTE_VOLUME_CONTROL,
                BT_HFP_HF_CODEC_TYPE_CVSD | BT_HFP_HF_CODEC_TYPE_MSBC);

    bt_hfp_ag_init(HMI_HFP_AG_RFC_CHANN_NUM, HMI_HSP_AG_RFC_CHANN_NUM,
                   BT_HFP_AG_LOCAL_CAPABILITY_3WAY              |
                   BT_HFP_AG_LOCAL_CAPABILITY_VOICE_RECOGNITION |
                   BT_HFP_AG_LOCAL_CAPABILITY_INBAND_RINGING    |
                   BT_HFP_AG_LOCAL_CAPABILITY_CODEC_NEGOTIATION |
                   BT_HFP_AG_LOCAL_CAPABILITY_HF_INDICATORS     |
                   BT_HFP_AG_LOCAL_CAPABILITY_ESCO_S4_SUPPORTED,
                   BT_HFP_AG_CODEC_TYPE_CVSD | BT_HFP_AG_CODEC_TYPE_MSBC);

    bt_mgr_cback_register(hmi_hfp_bt_cback);
}
