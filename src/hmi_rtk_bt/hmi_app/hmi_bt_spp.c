/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include "trace.h"
#include "bt_types.h"
#include "btm.h"
#include "bt_spp.h"
#include "bt_sdp.h"
#include "hmi_br_edr_link.h"
#include "hmi_bt_spp.h"

#define HMI_SPP_RFC_CHANN_NUM    0x03
#define HMI_SPP_DEFAULT_CREDITS  7

static const uint8_t hmi_spp_uuid128[16] =
{
    0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb
};

static const uint8_t hmi_spp_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x4C,
    /* SDP_ATTR_SRV_CLASS_ID_LIST */
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_SERIAL_PORT >> 8),
    (uint8_t)UUID_SERIAL_PORT,
    /* SDP_ATTR_PROTO_DESC_LIST */
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROTO_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROTO_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x0c,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_L2CAP >> 8),
    (uint8_t)UUID_L2CAP,
    SDP_DATA_ELEM_SEQ_HDR,
    0x05,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_RFCOMM >> 8),
    (uint8_t)UUID_RFCOMM,
    SDP_UNSIGNED_ONE_BYTE,
    HMI_SPP_RFC_CHANN_NUM,
    /* SDP_ATTR_BROWSE_GROUP_LIST */
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)UUID_PUBLIC_BROWSE_GROUP,
    /* SDP_ATTR_LANG_BASE_ATTR_ID_LIST */
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_LANG_BASE_ATTR_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_LANG_BASE_ATTR_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x09,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_LANG_ENGLISH >> 8),
    (uint8_t)SDP_LANG_ENGLISH,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_CHARACTER_UTF8 >> 8),
    (uint8_t)SDP_CHARACTER_UTF8,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_BASE_LANG_OFFSET >> 8),
    (uint8_t)SDP_BASE_LANG_OFFSET,
    /* SDP_ATTR_PROFILE_DESC_LIST */
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_SERIAL_PORT >> 8),
    (uint8_t)UUID_SERIAL_PORT,
    SDP_UNSIGNED_TWO_BYTE,
    0x01, 0x02,  /* version 1.2 */
    /* SDP_ATTR_SRV_NAME */
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)((SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET) >> 8),
    (uint8_t)(SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET),
    SDP_STRING_HDR,
    0x0B,
    0x73, 0x65, 0x72, 0x69, 0x61, 0x6c, 0x20, 0x70, 0x6f, 0x72, 0x74  /* "serial port" */
};

typedef struct
{
    bool     connected;
    uint8_t  local_server_chann;
    uint8_t  credit;
    uint16_t frame_size;
} T_HMI_SPP_CHAN;

static T_HMI_SPP_CHAN spp_chan[MAX_BR_LINK_NUM];

static void (*spp_rx_cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len);
static void (*spp_connect_cb)(uint8_t *bd_addr);
static void (*spp_disconnect_cb)(uint8_t *bd_addr);

static T_HMI_SPP_CHAN *find_spp_chan(uint8_t *bd_addr, uint8_t local_server_chann)
{
    T_BR_EDR_LINK *p_link = hmi_br_edr_find_link(bd_addr);

    if (p_link &&
        spp_chan[p_link->id].connected &&
        spp_chan[p_link->id].local_server_chann == local_server_chann)
    {
        return &spp_chan[p_link->id];
    }
    return NULL;
}

static void hmi_spp_bt_cback(T_BT_EVENT event_type, void *event_buf, uint16_t buf_len)
{
    T_BT_EVENT_PARAM *param = event_buf;
    T_BR_EDR_LINK    *p_link;
    T_HMI_SPP_CHAN   *p_chan;

    switch (event_type)
    {
    case BT_EVENT_SPP_CONN_IND:
        p_link = hmi_br_edr_find_link(param->spp_conn_ind.bd_addr);
        if (p_link != NULL)
        {
            p_chan = &spp_chan[p_link->id];
            bt_spp_connect_cfm(p_link->bd_addr,
                               param->spp_conn_ind.local_server_chann,
                               !p_chan->connected,
                               param->spp_conn_ind.frame_size,
                               HMI_SPP_DEFAULT_CREDITS);
        }
        break;

    case BT_EVENT_SPP_CONN_CMPL:
        p_link = hmi_br_edr_find_link(param->spp_conn_cmpl.bd_addr);
        if (p_link != NULL)
        {
            p_chan = &spp_chan[p_link->id];
            p_chan->connected          = true;
            p_chan->local_server_chann = param->spp_conn_cmpl.local_server_chann;
            p_chan->credit             = param->spp_conn_cmpl.link_credit;
            p_chan->frame_size         = param->spp_conn_cmpl.frame_size;
            APP_PRINT_INFO1("hmi_spp: connected, chann 0x%02x", p_chan->local_server_chann);
            if (spp_connect_cb != NULL)
            {
                spp_connect_cb(param->spp_conn_cmpl.bd_addr);
            }
        }
        break;

    case BT_EVENT_SPP_CREDIT_RCVD:
        p_chan = find_spp_chan(param->spp_credit_rcvd.bd_addr,
                               param->spp_credit_rcvd.local_server_chann);
        if (p_chan != NULL)
        {
            p_chan->credit = param->spp_credit_rcvd.link_credit;
        }
        break;

    case BT_EVENT_SPP_DATA_IND:
        p_chan = find_spp_chan(param->spp_data_ind.bd_addr,
                               param->spp_data_ind.local_server_chann);
        if (p_chan != NULL)
        {
            bt_spp_credits_give(param->spp_data_ind.bd_addr,
                                param->spp_data_ind.local_server_chann, 1);
            if (spp_rx_cb != NULL)
            {
                spp_rx_cb(param->spp_data_ind.bd_addr,
                          param->spp_data_ind.data,
                          param->spp_data_ind.len);
            }
        }
        break;

    case BT_EVENT_SPP_DISCONN_CMPL:
        p_chan = find_spp_chan(param->spp_disconn_cmpl.bd_addr,
                               param->spp_disconn_cmpl.local_server_chann);
        if (p_chan != NULL)
        {
            if (spp_disconnect_cb != NULL)
            {
                spp_disconnect_cb(param->spp_disconn_cmpl.bd_addr);
            }
            memset(p_chan, 0, sizeof(T_HMI_SPP_CHAN));
            APP_PRINT_INFO0("hmi_spp: disconnected");
        }
        break;

    default:
        break;
    }
}

void hmi_bt_spp_set_rx_cb(void (*cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len))
{
    spp_rx_cb = cb;
}

void hmi_bt_spp_set_connect_cb(void (*cb)(uint8_t *bd_addr))
{
    spp_connect_cb = cb;
}

void hmi_bt_spp_set_disconnect_cb(void (*cb)(uint8_t *bd_addr))
{
    spp_disconnect_cb = cb;
}

bool hmi_bt_spp_send(uint8_t *bd_addr, uint8_t *data, uint16_t len)
{
    T_BR_EDR_LINK  *p_link = hmi_br_edr_find_link(bd_addr);
    T_HMI_SPP_CHAN *p_chan;

    if (p_link == NULL)
    {
        return false;
    }

    p_chan = &spp_chan[p_link->id];
    if (!p_chan->connected || p_chan->credit == 0)
    {
        return false;
    }

    if (bt_spp_data_send(bd_addr, p_chan->local_server_chann, data, len, false))
    {
        p_chan->credit--;
        return true;
    }

    return false;
}

void hmi_bt_spp_init(void)
{
    memset(spp_chan, 0, sizeof(spp_chan));
    bt_sdp_record_add((void *)hmi_spp_sdp_record);
    bt_spp_init();
    bt_spp_service_register((uint8_t *)hmi_spp_uuid128, HMI_SPP_RFC_CHANN_NUM);
    bt_mgr_cback_register(hmi_spp_bt_cback);
    bt_spp_ertm_mode_set(false);
}
