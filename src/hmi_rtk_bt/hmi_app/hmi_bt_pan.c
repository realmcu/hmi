/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include "trace.h"
#include "bt_types.h"
#include "bt_sdp.h"
#include "bt_pan.h"
#include "hmi_br_edr_link.h"
#include "hmi_bt_pan.h"

#ifdef CONFIG_REALTEK_SUBSYS_LWIP
#include "bnepif.h"
#include "lwip/tcpip.h"
#endif

static uint8_t hmi_pan_local_addr[6];

static void (*pan_conn_cb)(uint8_t *bd_addr);
static void (*pan_disconn_cb)(uint8_t *bd_addr);
static void (*pan_rx_cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len);

static const uint8_t hmi_pan_panu_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x76,

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PANU >> 8),
    (uint8_t)(UUID_PANU),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROTO_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROTO_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x1E,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_L2CAP >> 8),
    (uint8_t)(UUID_L2CAP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(PSM_BNEP >> 8),
    (uint8_t)(PSM_BNEP),
    SDP_DATA_ELEM_SEQ_HDR,
    0x14,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_BNEP >> 8),
    (uint8_t)(UUID_BNEP),
    SDP_UNSIGNED_TWO_BYTE,
    0x01,
    0x00,
    SDP_DATA_ELEM_SEQ_HDR,
    0x0C,
    SDP_UNSIGNED_TWO_BYTE,
    0x08,
    0x00,
    SDP_UNSIGNED_TWO_BYTE,
    0x08,
    0x06,
    SDP_UNSIGNED_TWO_BYTE,
    0x81,
    0x00,
    SDP_UNSIGNED_TWO_BYTE,
    0x86,
    0xdd,

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_LANG_BASE_ATTR_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_LANG_BASE_ATTR_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x09,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_LANG_ENGLISH >> 8),
    (uint8_t)(SDP_LANG_ENGLISH),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_CHARACTER_UTF8 >> 8),
    (uint8_t)(SDP_CHARACTER_UTF8),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_BASE_LANG_OFFSET >> 8),
    (uint8_t)(SDP_BASE_LANG_OFFSET),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PANU >> 8),
    (uint8_t)(UUID_PANU),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0100 >> 8),
    (uint8_t)(0x0100),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)((SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET) >> 8),
    (uint8_t)(SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET),
    SDP_STRING_HDR,
    0x08,
    'H', 'M', 'I', ' ', 'P', 'A', 'N', 'U',

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)((SDP_ATTR_SRV_DESC + SDP_BASE_LANG_OFFSET) >> 8),
    (uint8_t)(SDP_ATTR_SRV_DESC + SDP_BASE_LANG_OFFSET),
    SDP_STRING_HDR,
    0x08,
    'H', 'M', 'I', ' ', 'P', 'A', 'N', 'U',

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SECURITY_DESC >> 8),
    (uint8_t)SDP_ATTR_SECURITY_DESC,
    SDP_UNSIGNED_TWO_BYTE,
    0x00,
    0x00
};

static const uint8_t hmi_pan_nap_sdp_record[] =
{
    SDP_DATA_ELEM_SEQ_HDR,
    0x85,

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_NAP >> 8),
    (uint8_t)(UUID_NAP),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROTO_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROTO_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x1E,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_L2CAP >> 8),
    (uint8_t)(UUID_L2CAP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(PSM_BNEP >> 8),
    (uint8_t)(PSM_BNEP),
    SDP_DATA_ELEM_SEQ_HDR,
    0x14,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_BNEP >> 8),
    (uint8_t)(UUID_BNEP),
    SDP_UNSIGNED_TWO_BYTE,
    0x01,
    0x00,
    SDP_DATA_ELEM_SEQ_HDR,
    0x0C,
    SDP_UNSIGNED_TWO_BYTE,
    0x08,
    0x00,
    SDP_UNSIGNED_TWO_BYTE,
    0x08,
    0x06,
    SDP_UNSIGNED_TWO_BYTE,
    0x81,
    0x00,
    SDP_UNSIGNED_TWO_BYTE,
    0x86,
    0xdd,

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_BROWSE_GROUP_LIST >> 8),
    (uint8_t)SDP_ATTR_BROWSE_GROUP_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x03,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP >> 8),
    (uint8_t)(UUID_PUBLIC_BROWSE_GROUP),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_LANG_BASE_ATTR_ID_LIST >> 8),
    (uint8_t)SDP_ATTR_LANG_BASE_ATTR_ID_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x09,
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_LANG_ENGLISH >> 8),
    (uint8_t)(SDP_LANG_ENGLISH),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_CHARACTER_UTF8 >> 8),
    (uint8_t)(SDP_CHARACTER_UTF8),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_BASE_LANG_OFFSET >> 8),
    (uint8_t)(SDP_BASE_LANG_OFFSET),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8),
    (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
    SDP_DATA_ELEM_SEQ_HDR,
    0x08,
    SDP_DATA_ELEM_SEQ_HDR,
    0x06,
    SDP_UUID16_HDR,
    (uint8_t)(UUID_NAP >> 8),
    (uint8_t)(UUID_NAP),
    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(0x0100 >> 8),
    (uint8_t)(0x0100),

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)((SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET) >> 8),
    (uint8_t)(SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET),
    SDP_STRING_HDR,
    0x07,
    'H', 'M', 'I', ' ', 'N', 'A', 'P',

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)((SDP_ATTR_SRV_DESC + SDP_BASE_LANG_OFFSET) >> 8),
    (uint8_t)(SDP_ATTR_SRV_DESC + SDP_BASE_LANG_OFFSET),
    SDP_STRING_HDR,
    0x07,
    'H', 'M', 'I', ' ', 'N', 'A', 'P',

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_SECURITY_DESC >> 8),
    (uint8_t)SDP_ATTR_SECURITY_DESC,
    SDP_UNSIGNED_TWO_BYTE,
    0x00,
    0x01,

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_NET_ACCESS_TYPE >> 8),
    (uint8_t)SDP_ATTR_NET_ACCESS_TYPE,
    SDP_UNSIGNED_TWO_BYTE,
    0x00,
    0x05,

    SDP_UNSIGNED_TWO_BYTE,
    (uint8_t)(SDP_ATTR_MAX_NET_ACCESS_RATE >> 8),
    (uint8_t)SDP_ATTR_MAX_NET_ACCESS_RATE,
    SDP_UNSIGNED_FOUR_BYTE,
    0x00,
    0x13,
    0x12,
    0xd0
};

static void hmi_pan_bt_cback(T_BT_PAN_EVENT event_type, void *event_buf, uint16_t buf_len)
{
    T_BT_PAN_EVENT_PARAM *param = event_buf;

    switch (event_type)
    {
    case BT_PAN_EVENT_CONN_IND:
        bt_pan_connect_cfm(hmi_pan_local_addr, param->pan_conn_ind.bd_addr, true);
        break;

    case BT_PAN_EVENT_SETUP_CONN_IND:
        bt_pan_setup_connection_rsp(param->pan_conn_ind.bd_addr, 0);
        break;

    case BT_PAN_EVENT_CONN_CMPL:
        APP_PRINT_INFO0("hmi_pan: connected");
        if (pan_conn_cb != NULL)
        {
            pan_conn_cb(param->pan_conn_cmpl.bd_addr);
        }
#ifdef CONFIG_REALTEK_SUBSYS_LWIP
        bnepif_netif_up(param->pan_conn_cmpl.bd_addr);
        bnepif_dhcp_start();
#endif
        break;

    case BT_PAN_EVENT_DISCONN_CMPL:
        APP_PRINT_INFO0("hmi_pan: disconnected");
        if (pan_disconn_cb != NULL)
        {
            pan_disconn_cb(param->pan_disconn_cmpl.bd_addr);
        }
#ifdef CONFIG_REALTEK_SUBSYS_LWIP
        bnepif_netif_down();
#endif
        break;

    case BT_PAN_EVENT_ETHERNET_PACKET_IND:
        if (pan_rx_cb != NULL)
        {
            pan_rx_cb(param->pan_ethernet_packet_ind.bd_addr,
                      param->pan_ethernet_packet_ind.buf,
                      param->pan_ethernet_packet_ind.len);
        }
#ifdef CONFIG_REALTEK_SUBSYS_LWIP
        else
        {
            bnepif_low_level_input(param->pan_ethernet_packet_ind.buf,
                                   param->pan_ethernet_packet_ind.len);
        }
#endif
        break;

    default:
        break;
    }
}

void hmi_bt_pan_set_conn_cb(void (*cb)(uint8_t *bd_addr))
{
    pan_conn_cb = cb;
}

void hmi_bt_pan_set_disconn_cb(void (*cb)(uint8_t *bd_addr))
{
    pan_disconn_cb = cb;
}

void hmi_bt_pan_set_rx_cb(void (*cb)(uint8_t *bd_addr, uint8_t *data, uint16_t len))
{
    pan_rx_cb = cb;
}

void hmi_bt_pan_init(uint8_t *local_bd_addr)
{
    memcpy(hmi_pan_local_addr, local_bd_addr, 6);

    bt_sdp_record_add((void *)hmi_pan_panu_sdp_record);
    bt_sdp_record_add((void *)hmi_pan_nap_sdp_record);

    bt_pan_init();
    bt_pan_cback_register(hmi_pan_bt_cback);

#ifdef CONFIG_REALTEK_SUBSYS_LWIP
    tcpip_init(NULL, NULL);
    bnepif_init(local_bd_addr, bt_pan_send);
#endif
}
