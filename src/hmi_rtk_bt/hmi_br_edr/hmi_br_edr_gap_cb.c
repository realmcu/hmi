/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include "trace.h"
#include "gap.h"
#include "gap_br.h"
#include "btm.h"
#include "bt_bond.h"
#include "hmi_br_edr_gap_cb.h"
#include "hmi_br_edr_link.h"

void hmi_br_edr_gap_common_cb(uint8_t cb_type, void *p_cb_data)
{
    T_GAP_CB_DATA cb_data;
    memcpy(&cb_data, p_cb_data, sizeof(T_GAP_CB_DATA));
    APP_PRINT_INFO1("hmi_br_edr_gap_common_cb: cb_type = %d", cb_type);

    switch (cb_type)
    {
    case GAP_MSG_WRITE_AIRPLAN_MODE:
        APP_PRINT_INFO1("hmi_br_edr_gap_common_cb: GAP_MSG_WRITE_AIRPLAN_MODE cause 0x%04x",
                        cb_data.p_gap_write_airplan_mode_rsp->cause);
        break;

    case GAP_MSG_READ_AIRPLAN_MODE:
        APP_PRINT_INFO2("hmi_br_edr_gap_common_cb: GAP_MSG_READ_AIRPLAN_MODE cause 0x%04x mode %d",
                        cb_data.p_gap_read_airplan_mode_rsp->cause,
                        cb_data.p_gap_read_airplan_mode_rsp->mode);
        break;

    case GAP_MSG_VENDOR_CMD_CMPL_EVENT:
        break;

    case GAP_MSG_SET_LOCAL_BD_ADDR:
        APP_PRINT_INFO1("hmi_br_edr_gap_common_cb: GAP_MSG_SET_LOCAL_BD_ADDR cause 0x%04x",
                        cb_data.p_gap_set_bd_addr_rsp->cause);
        break;

    default:
        break;
    }
}

void hmi_br_edr_gap_bt_cback(T_BT_EVENT event_type, void *event_buf, uint16_t buf_len)
{
    T_BT_EVENT_PARAM *param = event_buf;
    T_BR_EDR_LINK *p_link;
    bool handle = true;

    switch (event_type)
    {
    case BT_EVENT_READY:
        {
            APP_PRINT_INFO1("hmi_br_edr_gap_bt_cback: BT_EVENT_READY bd_addr %b",
                            TRACE_BDADDR(param->ready.bd_addr));
        }
        break;

    case BT_EVENT_ACL_CONN_IND:
        {
            p_link = hmi_br_edr_find_link(param->acl_conn_ind.bd_addr);
            if (p_link != NULL)
            {
                /* already connected, reject duplicate */
                bt_acl_conn_reject(param->acl_conn_ind.bd_addr, BT_ACL_REJECT_UNACCEPTABLE_ADDR);
            }
            else
            {
                bt_acl_conn_accept(param->acl_conn_ind.bd_addr, BT_LINK_ROLE_SLAVE);
            }
        }
        break;

    case BT_EVENT_LINK_KEY_REQ:
        {
            uint8_t link_key[16];
            T_BT_LINK_KEY_TYPE type;

            if (bt_bond_key_get(param->link_key_req.bd_addr, link_key, (uint8_t *)&type))
            {
                bt_link_key_cfm(param->link_key_req.bd_addr, true, type, link_key);
            }
            else
            {
                bt_link_key_cfm(param->link_key_req.bd_addr, false, type, link_key);
            }
        }
        break;

    case BT_EVENT_LINK_PIN_CODE_REQ:
        {
            uint8_t pin_code[4] = {1, 2, 3, 4};
            bt_link_pin_code_cfm(param->link_pin_code_req.bd_addr, pin_code, 4, true);
        }
        break;

    case BT_EVENT_LINK_USER_CONFIRMATION_REQ:
        {
            gap_br_user_cfm_req_cfm(param->link_user_confirmation_req.bd_addr,
                                    GAP_CFM_CAUSE_ACCEPT);
        }
        break;

    case BT_EVENT_ACL_CONN_SUCCESS:
        {
            hmi_br_edr_alloc_link(param->acl_conn_success.bd_addr);
            bt_device_mode_set(BT_DEVICE_MODE_IDLE);
            bt_active_link_set(param->acl_conn_success.bd_addr);
        }
        break;

    case BT_EVENT_ACL_CONN_DISCONN:
        {
            p_link = hmi_br_edr_find_link(param->acl_conn_disconn.bd_addr);
            if (p_link != NULL)
            {
                hmi_br_edr_free_link(p_link);
            }

            bt_device_mode_set(BT_DEVICE_MODE_DISCOVERABLE_CONNECTABLE);
        }
        break;

    case BT_EVENT_INQUIRY_RSP:
        APP_PRINT_INFO1("hmi_br_edr_gap_bt_cback: BT_EVENT_INQUIRY_RSP cause 0x%x",
                        param->inquiry_rsp.cause);
        break;

    case BT_EVENT_INQUIRY_RESULT:
        APP_PRINT_INFO6("hmi_br_edr_gap_bt_cback: BT_EVENT_INQUIRY_RESULT addr [%02x:%02x:%02x:%02x:%02x:%02x]",
                        param->inquiry_result.bd_addr[5], param->inquiry_result.bd_addr[4],
                        param->inquiry_result.bd_addr[3], param->inquiry_result.bd_addr[2],
                        param->inquiry_result.bd_addr[1], param->inquiry_result.bd_addr[0]);
        break;

    case BT_EVENT_INQUIRY_CMPL:
        APP_PRINT_INFO0("hmi_br_edr_gap_bt_cback: BT_EVENT_INQUIRY_CMPL");
        break;

    case BT_EVENT_INQUIRY_CANCEL_RSP:
        APP_PRINT_INFO0("hmi_br_edr_gap_bt_cback: BT_EVENT_INQUIRY_CANCEL_RSP");
        break;

    case BT_EVENT_DEVICE_MODE_RSP:
        break;

    default:
        handle = false;
        break;
    }

    if (handle)
    {
        APP_PRINT_INFO1("hmi_br_edr_gap_bt_cback: event_type 0x%04x", event_type);
    }
}
