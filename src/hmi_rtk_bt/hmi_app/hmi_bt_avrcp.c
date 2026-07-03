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
#include "bt_avrcp.h"
#include "hmi_br_edr_link.h"
#include "hmi_bt_avrcp.h"

typedef struct
{
    bool    connected;
    bool    is_streaming;
    uint8_t volume;       /* 0-127 (BT absolute volume scale) */
} T_HMI_AVRCP_LINK;

static T_HMI_AVRCP_LINK avrcp_link[MAX_BR_LINK_NUM];

static void (*avrcp_play_status_cb)(uint8_t *bd_addr, uint8_t play_status);
static void (*avrcp_volume_cb)(uint8_t *bd_addr, uint8_t vol_0_127);

/* CT SDP: UUID_AV_REMOTE_CONTROL + UUID_AV_REMOTE_CONTROL_CONTROLLER, Cat1+Cat2 */
static const uint8_t hmi_avrcp_ct_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x3b,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AV_REMOTE_CONTROL >> 8),
    (uint8_t)(UUID_AV_REMOTE_CONTROL),
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AV_REMOTE_CONTROL_CONTROLLER >> 8),
    (uint8_t)(UUID_AV_REMOTE_CONTROL_CONTROLLER & 0xff),
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
    (uint8_t)(PSM_AVCTP >> 8),
    (uint8_t)PSM_AVCTP,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AVCTP >> 8),
    (uint8_t)(UUID_AVCTP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0104 >> 8),
    (uint8_t)(0x0104),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AV_REMOTE_CONTROL >> 8),
    (uint8_t)(UUID_AV_REMOTE_CONTROL),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0106 >> 8),
    (uint8_t)(0x0106),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES >> 8),
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0003 >> 8),
    (uint8_t)(0x0003 & 0xff),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)UUID_PUBLIC_BROWSE_GROUP
};

/* TG SDP: UUID_AV_REMOTE_CONTROL_TARGET, Cat1 */
static const uint8_t hmi_avrcp_tg_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x38,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AV_REMOTE_CONTROL_TARGET >> 8),
    (uint8_t)(UUID_AV_REMOTE_CONTROL_TARGET),
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
    (uint8_t)(PSM_AVCTP >> 8),
    (uint8_t)PSM_AVCTP,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AVCTP >> 8),
    (uint8_t)(UUID_AVCTP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0104 >> 8),
    (uint8_t)(0x0104),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_AV_REMOTE_CONTROL >> 8),
    (uint8_t)(UUID_AV_REMOTE_CONTROL),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0106 >> 8),
    (uint8_t)(0x0106),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES >> 8),
    (uint8_t)(SDP_ATTR_SUPPORTED_FEATURES),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0001 >> 8),
    (uint8_t)(0x0001 & 0xff),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)UUID_PUBLIC_BROWSE_GROUP
};

static void hmi_avrcp_bt_cback(T_BT_EVENT event_type, void *event_buf, uint16_t buf_len)
{
    T_BT_EVENT_PARAM  *param = event_buf;
    T_BR_EDR_LINK     *p_link;
    T_HMI_AVRCP_LINK  *p_avrcp;

    switch (event_type)
    {
    case BT_EVENT_AVRCP_CONN_IND:
        p_link = hmi_br_edr_find_link(param->avrcp_conn_ind.bd_addr);
        if (p_link != NULL)
        {
            bt_avrcp_connect_cfm(p_link->bd_addr, true);
        }
        break;

    case BT_EVENT_AVRCP_CONN_CMPL:
        p_link = hmi_br_edr_find_link(param->avrcp_conn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            avrcp_link[p_link->id].connected = true;
            avrcp_link[p_link->id].volume    = 0x7F;
            APP_PRINT_INFO0("hmi_avrcp: connected");
        }
        break;

    case BT_EVENT_AVRCP_DISCONN_CMPL:
        p_link = hmi_br_edr_find_link(param->avrcp_disconn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            memset(&avrcp_link[p_link->id], 0, sizeof(T_HMI_AVRCP_LINK));
            APP_PRINT_INFO0("hmi_avrcp: disconnected");
        }
        break;

    case BT_EVENT_AVRCP_PLAY_STATUS_CHANGED_REG_REQ:
        /* Remote CT registered for play status; respond with our current status */
        p_link = hmi_br_edr_find_link(param->avrcp_reg_play_status_changed.bd_addr);
        if (p_link != NULL)
        {
            p_avrcp = &avrcp_link[p_link->id];
            bt_avrcp_play_status_change_register_rsp(p_link->bd_addr,
                                                     p_avrcp->is_streaming ? BT_AVRCP_PLAY_STATUS_PLAYING
                                                     : BT_AVRCP_PLAY_STATUS_PAUSED);
        }
        break;

    case BT_EVENT_AVRCP_PLAY_STATUS_CHANGED:
        p_link = hmi_br_edr_find_link(param->avrcp_play_status_changed.bd_addr);
        if (p_link != NULL && avrcp_play_status_cb != NULL)
        {
            avrcp_play_status_cb(param->avrcp_play_status_changed.bd_addr,
                                 param->avrcp_play_status_changed.play_status);
        }
        break;

    case BT_EVENT_AVRCP_ABSOLUTE_VOLUME_SET:
        p_link = hmi_br_edr_find_link(param->avrcp_absolute_volume_set.bd_addr);
        if (p_link != NULL)
        {
            avrcp_link[p_link->id].volume = param->avrcp_absolute_volume_set.volume;
            if (avrcp_volume_cb != NULL)
            {
                avrcp_volume_cb(param->avrcp_absolute_volume_set.bd_addr,
                                param->avrcp_absolute_volume_set.volume);
            }
        }
        break;

    case BT_EVENT_AVRCP_REG_VOLUME_CHANGED:
        /* Remote CT registered for volume; respond with our current volume */
        p_link = hmi_br_edr_find_link(param->avrcp_reg_volume_changed.bd_addr);
        if (p_link != NULL)
        {
            bt_avrcp_volume_change_register_rsp(p_link->bd_addr,
                                                avrcp_link[p_link->id].volume);
        }
        break;

    default:
        break;
    }
}

void hmi_bt_avrcp_play_status_update(uint8_t *bd_addr, bool playing)
{
    T_BR_EDR_LINK *p_link = hmi_br_edr_find_link(bd_addr);

    if (p_link == NULL || !avrcp_link[p_link->id].connected)
    {
        return;
    }

    if (avrcp_link[p_link->id].is_streaming != playing)
    {
        avrcp_link[p_link->id].is_streaming = playing;
        bt_avrcp_play_status_change_req(bd_addr,
                                        playing ? BT_AVRCP_PLAY_STATUS_PLAYING : BT_AVRCP_PLAY_STATUS_PAUSED);
    }
}

void hmi_bt_avrcp_volume_update(uint8_t *bd_addr, uint8_t vol_0_127)
{
    T_BR_EDR_LINK *p_link = hmi_br_edr_find_link(bd_addr);

    if (p_link == NULL || !avrcp_link[p_link->id].connected)
    {
        return;
    }

    avrcp_link[p_link->id].volume = vol_0_127;
    bt_avrcp_volume_change_req(bd_addr, vol_0_127);
}

void hmi_bt_avrcp_set_play_status_cb(void (*cb)(uint8_t *bd_addr, uint8_t play_status))
{
    avrcp_play_status_cb = cb;
}

void hmi_bt_avrcp_set_volume_cb(void (*cb)(uint8_t *bd_addr, uint8_t vol_0_127))
{
    avrcp_volume_cb = cb;
}

void hmi_bt_avrcp_init(void)
{
    memset(avrcp_link, 0, sizeof(avrcp_link));

    bt_sdp_record_add((void *)hmi_avrcp_ct_sdp_record);
    bt_sdp_record_add((void *)hmi_avrcp_tg_sdp_record);

    bt_avrcp_init(BT_AVRCP_FEATURE_CATEGORY_1 | BT_AVRCP_FEATURE_CATEGORY_2,
                  BT_AVRCP_FEATURE_CATEGORY_1);

    bt_mgr_cback_register(hmi_avrcp_bt_cback);
}
