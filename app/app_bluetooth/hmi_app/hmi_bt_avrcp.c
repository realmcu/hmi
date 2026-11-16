/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#include <string.h>
#include <stdlib.h>
#include <os_sync.h>
#include "trace.h"
#include "bt_types.h"
#include "btm.h"
#include "bt_sdp.h"
#include "bt_avrcp.h"
#include "hmi_br_edr_link.h"
#include "hmi_bt_avrcp.h"
#include "hmi_bt_task.h"
#include "app_event.h"
#include "app_event_defs.h"

typedef struct
{
    bool    connected;
    bool    is_streaming;
    uint8_t volume;       /* 0-127 (BT absolute volume scale) */
} T_HMI_AVRCP_LINK;

static T_HMI_AVRCP_LINK avrcp_link[MAX_BR_LINK_NUM];
static hmi_media_snapshot_t media;
static uint8_t local_volume = 68; /* 0..127, about 8/15 speaker level */

static void (*avrcp_play_status_cb)(uint8_t *bd_addr, uint8_t play_status);
static void (*avrcp_volume_cb)(uint8_t *bd_addr, uint8_t vol_0_127);

static void media_copy_utf8(char *dst, size_t capacity, const uint8_t *src, size_t length)
{
    size_t n = 0;
    if (capacity == 0)
    {
        return;
    }
    dst[0] = '\0';
    if (src == NULL)
    {
        return;
    }
    while (src != NULL && n < length && n + 1 < capacity)
    {
        uint8_t c = src[n];
        size_t bytes = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
                       (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
        if (n + bytes > length || n + bytes >= capacity)
        {
            break;
        }
        n += bytes;
    }
    if (n != 0)
    {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

void hmi_media_snapshot(hmi_media_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }
    uint32_t key = os_lock();
    *snapshot = media;
    os_unlock(key);
}

bool hmi_media_send(hmi_media_command_t command)
{
    hmi_media_command_t *queued = malloc(sizeof(*queued));
    if (queued == NULL)
    {
        return false;
    }
    *queued = command;
    T_IO_MSG msg = {0};
    msg.type = HMI_MEDIA_IO_MSG_TYPE;
    msg.u.buf = queued;
    if (!hmi_send_msg_to_bt_task(&msg))
    {
        free(queued);
        return false;
    }
    return true;
}

bool hmi_media_set_volume(uint8_t percent)
{
    uint8_t *queued = malloc(sizeof(*queued));
    if (queued == NULL)
    {
        return false;
    }
    *queued = percent > 100u ? 100u : percent;
    T_IO_MSG msg = {0};
    msg.type = HMI_MEDIA_VOLUME_IO_MSG_TYPE;
    msg.u.buf = queued;
    if (!hmi_send_msg_to_bt_task(&msg))
    {
        free(queued);
        return false;
    }
    return true;
}

static void media_apply_local_volume(uint8_t level)
{
    if (level > 0x7Fu)
    {
        level = 0x7Fu;
    }
    local_volume = level;
    uint8_t percent = (uint8_t)(((unsigned)level * 100u + 63u) / 127u);
    if (avrcp_volume_cb != NULL)
    {
        avrcp_volume_cb(NULL, level);
    }
    (void)app_event_publish(EVT_MEDIA_VOLUME_CHANGED, &percent, sizeof(percent));
    for (uint8_t i = 0; i < MAX_BR_LINK_NUM; ++i)
    {
        T_BR_EDR_LINK *link = &hmi_br_edr_app_db.br_link[i];
        if (link->used && avrcp_link[link->id].connected)
        {
            avrcp_link[link->id].volume = level;
            (void)bt_avrcp_volume_change_req(link->bd_addr, level);
            break;
        }
    }
}

void hmi_media_handle_volume(void *value)
{
    uint8_t *queued = value;
    if (queued == NULL)
    {
        return;
    }
    uint8_t percent = *queued > 100u ? 100u : *queued;
    free(queued);
    media_apply_local_volume((uint8_t)(((unsigned)percent * 127u + 50u) / 100u));
}

static void media_request_current(uint8_t *bd_addr)
{
    uint8_t attrs[] = { BT_AVRCP_ELEM_ATTR_TITLE, BT_AVRCP_ELEM_ATTR_ARTIST };
    (void)bt_avrcp_get_element_attr_req(bd_addr, sizeof(attrs), attrs);
    (void)bt_avrcp_get_play_status_req(bd_addr);
}

void hmi_media_handle_command(void *command)
{
    hmi_media_command_t *queued = command;
    if (queued == NULL)
    {
        return;
    }

    for (uint8_t i = 0; i < MAX_BR_LINK_NUM; ++i)
    {
        T_BR_EDR_LINK *link = &hmi_br_edr_app_db.br_link[i];
        if (!link->used || !avrcp_link[link->id].connected)
        {
            continue;
        }

        switch (*queued)
        {
        case HMI_MEDIA_TOGGLE:
            (void)(avrcp_link[link->id].is_streaming ?
                   bt_avrcp_pause(link->bd_addr) : bt_avrcp_play(link->bd_addr));
            break;
        case HMI_MEDIA_PREVIOUS:
            (void)bt_avrcp_backward(link->bd_addr);
            break;
        case HMI_MEDIA_NEXT:
            (void)bt_avrcp_forward(link->bd_addr);
            break;
        case HMI_MEDIA_VOLUME_DOWN:
            media_apply_local_volume(local_volume > 8u ? local_volume - 8u : 0u);
            break;
        case HMI_MEDIA_VOLUME_UP:
            media_apply_local_volume(local_volume < 119u ? local_volume + 8u : 127u);
            break;
        case HMI_MEDIA_REFRESH:
            media_request_current(link->bd_addr);
            break;
        default:
            break;
        }
        break;
    }
    free(queued);
}

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
            avrcp_link[p_link->id].volume    = local_volume;
            uint32_t key = os_lock();
            media.connected = true;
            os_unlock(key);
            (void)bt_avrcp_register_notification_req(
                p_link->bd_addr, BT_AVRCP_EVENT_ID_PLAYBACK_STATUS_CHANGED);
            media_request_current(p_link->bd_addr);
            {
                uint8_t state = 2u;
                (void)app_event_publish(EVT_MEDIA_STATE_CHANGED, &state, sizeof(state));
            }
            APP_PRINT_INFO0("hmi_avrcp: connected");
        }
        break;

    case BT_EVENT_AVRCP_DISCONN_CMPL:
        p_link = hmi_br_edr_find_link(param->avrcp_disconn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            memset(&avrcp_link[p_link->id], 0, sizeof(T_HMI_AVRCP_LINK));
            uint32_t key = os_lock();
            memset(&media, 0, sizeof(media));
            os_unlock(key);
            uint8_t state = 0u;
            (void)app_event_publish(EVT_MEDIA_STATE_CHANGED, &state, sizeof(state));
            (void)app_event_publish(EVT_MEDIA_METADATA, NULL, 0);
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
        if (p_link != NULL)
        {
            bool playing = param->avrcp_play_status_changed.play_status ==
                           BT_AVRCP_PLAY_STATUS_PLAYING;
            uint8_t state = playing ? 1u : 2u;
            avrcp_link[p_link->id].is_streaming = playing;
            uint32_t key = os_lock();
            media.playing = playing;
            os_unlock(key);
            (void)app_event_publish(EVT_MEDIA_STATE_CHANGED, &state, sizeof(state));
            if (avrcp_play_status_cb != NULL)
            {
                avrcp_play_status_cb(param->avrcp_play_status_changed.bd_addr,
                                     param->avrcp_play_status_changed.play_status);
            }
        }
        break;

    case BT_EVENT_AVRCP_TRACK_CHANGED:
        p_link = hmi_br_edr_find_link(param->avrcp_track_changed.bd_addr);
        if (p_link != NULL)
        {
            media_request_current(p_link->bd_addr);
        }
        break;

    case BT_EVENT_AVRCP_ELEM_ATTR:
        p_link = hmi_br_edr_find_link(param->avrcp_elem_attr.bd_addr);
        if (p_link != NULL && param->avrcp_elem_attr.state == 0 &&
            param->avrcp_elem_attr.attr != NULL)
        {
            uint32_t key = os_lock();
            for (uint8_t i = 0; i < param->avrcp_elem_attr.num_of_attr; ++i)
            {
                T_BT_AVRCP_ELEMENT_ATTR *attr = &param->avrcp_elem_attr.attr[i];
                if (attr->attribute_id == BT_AVRCP_ELEM_ATTR_TITLE)
                {
                    media_copy_utf8(media.title, sizeof(media.title), attr->p_buf, attr->length);
                }
                else if (attr->attribute_id == BT_AVRCP_ELEM_ATTR_ARTIST)
                {
                    media_copy_utf8(media.artist, sizeof(media.artist), attr->p_buf, attr->length);
                }
            }
            os_unlock(key);
            (void)app_event_publish(EVT_MEDIA_METADATA, NULL, 0);
        }
        break;

    case BT_EVENT_AVRCP_GET_PLAY_STATUS_RSP:
        p_link = hmi_br_edr_find_link(param->avrcp_get_play_status_rsp.bd_addr);
        if (p_link != NULL)
        {
            uint32_t key = os_lock();
            media.has_position = true;
            media.position_ms = param->avrcp_get_play_status_rsp.position_ms;
            media.length_ms = param->avrcp_get_play_status_rsp.length_ms;
            media.playing = param->avrcp_get_play_status_rsp.play_status ==
                            BT_AVRCP_PLAY_STATUS_PLAYING;
            os_unlock(key);
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
        uint32_t key = os_lock();
        media.playing = playing;
        os_unlock(key);
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
    memset(&media, 0, sizeof(media));

    bt_sdp_record_add((void *)hmi_avrcp_ct_sdp_record);
    bt_sdp_record_add((void *)hmi_avrcp_tg_sdp_record);

    bt_avrcp_init(BT_AVRCP_FEATURE_CATEGORY_1 | BT_AVRCP_FEATURE_CATEGORY_2,
                  BT_AVRCP_FEATURE_CATEGORY_1);

    bt_mgr_cback_register(hmi_avrcp_bt_cback);
}
