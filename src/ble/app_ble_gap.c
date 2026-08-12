/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <trace.h>
#include <string.h>
#include <gap_msg.h>
#include <ble_mgr.h>
#include <gap_bond_le.h>
#include <app_ble_gap.h>
#include "app_ble_adv.h"
#include <gap_conn_le.h>
#include "bt_bond_le.h"
#include "bt_bond_common.h"
#include "gap_bond_manager.h"
#include "bt_bond_api.h"
#if F_APP_BT_GATT_CLIENT_SUPPORT
#include "bt_gatt_client.h"
#endif

T_GAP_DEV_STATE gap_dev_state = { 0, 0, 0, 0 };
T_GAP_CONN_STATE gap_conn_state = GAP_CONN_STATE_DISCONNECTED;

void app_ble_bond_cb(uint8_t cb_type, void *p_cb_data)
{
    T_BT_LE_BOND_CB_DATA cb_data;
    memcpy(&cb_data, p_cb_data, sizeof(T_BT_LE_BOND_CB_DATA));

    switch (cb_type)
    {
    case BT_BOND_MSG_LE_BOND_ADD:
    {
        APP_PRINT_INFO3(
            "BT_BOND_MSG_LE_BOND_ADD: modify_flags 0x%x, remote_bd_type %d, remote_bd %s",
            cb_data.p_le_bond_add->modify_flags,
            cb_data.p_le_bond_add->p_entry->remote_bd_type,
            TRACE_BDADDR(cb_data.p_le_bond_add->p_entry->remote_bd));
    }
    break;

    case BT_BOND_MSG_LE_BOND_REMOVE:
    {
        APP_PRINT_INFO2("BT_BOND_MSG_LE_BOND_REMOVE: remote_bd_type %d, remote_bd %s",
                        cb_data.p_le_bond_remove->p_entry->remote_bd_type,
                        TRACE_BDADDR(cb_data.p_le_bond_remove->p_entry->remote_bd));
    }
    break;

    case BT_BOND_MSG_LE_BOND_CLEAR:
    {
        APP_PRINT_INFO0("BT_BOND_MSG_LE_BOND_CLEAR");
    }
    break;

    default:
        break;
    }
    return;
}

static void app_ble_gap_ble_mgr_init(void)
{
    BLE_MGR_PARAMS param = { 0 };
    param.ble_ext_adv.enable = true;
    param.ble_ext_adv.adv_num = 1;
    ble_mgr_init(&param);
    bt_bond_register_app_cb(app_ble_bond_cb);
}

void app_ble_gap_init(void)
{
    uint8_t device_name[GAP_DEVICE_NAME_LEN] = "BLE_NAVI";
    uint16_t appearance = GAP_GATT_APPEARANCE_UNKNOWN;

    le_set_gap_param(GAP_PARAM_DEVICE_NAME, GAP_DEVICE_NAME_LEN, device_name);
    le_set_gap_param(GAP_PARAM_APPEARANCE, sizeof(appearance), &appearance);

    /* Keep MTU exchange client-initiated. */
    uint8_t slave_init_mtu_req = false;
    le_set_gap_param(
        GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ, sizeof(slave_init_mtu_req), &slave_init_mtu_req);

    /* Pair without user interaction and persist the resulting bond. */
    uint8_t auth_pair_mode = GAP_PAIRING_MODE_PAIRABLE;
    uint16_t auth_flags = GAP_AUTHEN_BIT_BONDING_FLAG;
    uint8_t auth_io_cap = GAP_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint8_t auth_oob = false;
    uint8_t auth_use_fix_passkey = false;
    uint32_t auth_fix_passkey = 0;
#if F_APP_BT_ANCS_CLIENT_SUPPORT
    uint8_t auth_sec_req_enable = true;
#else
    uint8_t auth_sec_req_enable = false;
#endif
    uint16_t auth_sec_req_flags = GAP_AUTHEN_BIT_BONDING_FLAG;

    gap_set_param(GAP_PARAM_BOND_PAIRING_MODE, sizeof(auth_pair_mode), &auth_pair_mode);
    gap_set_param(GAP_PARAM_BOND_AUTHEN_REQUIREMENTS_FLAGS, sizeof(auth_flags), &auth_flags);
    gap_set_param(GAP_PARAM_BOND_IO_CAPABILITIES, sizeof(auth_io_cap), &auth_io_cap);
    gap_set_param(GAP_PARAM_BOND_OOB_ENABLED, sizeof(auth_oob), &auth_oob);
    le_bond_set_param(GAP_PARAM_BOND_FIXED_PASSKEY, sizeof(auth_fix_passkey), &auth_fix_passkey);
    le_bond_set_param(
        GAP_PARAM_BOND_FIXED_PASSKEY_ENABLE, sizeof(auth_use_fix_passkey), &auth_use_fix_passkey);
    le_bond_set_param(
        GAP_PARAM_BOND_SEC_REQ_ENABLE, sizeof(auth_sec_req_enable), &auth_sec_req_enable);
    le_bond_set_param(
        GAP_PARAM_BOND_SEC_REQ_REQUIREMENT, sizeof(auth_sec_req_flags), &auth_sec_req_flags);

    le_register_app_cb(app_ble_gap_callback);

    app_ble_gap_ble_mgr_init();

    app_ble_adv_init_conn_public();
}

static void app_ble_gap_handle_dev_state_evt(T_GAP_DEV_STATE new_state, uint16_t cause)
{
    APP_PRINT_INFO3("app_ble_gap_handle_dev_state_evt: init state %d, adv state %d, cause 0x%x",
                    new_state.gap_init_state,
                    new_state.gap_adv_state,
                    cause);
    if (gap_dev_state.gap_init_state != new_state.gap_init_state)
    {
        if (new_state.gap_init_state == GAP_INIT_STATE_STACK_READY)
        {
            APP_PRINT_INFO0("app_ble_gap_handle_dev_state_evt: GAP stack ready");
            app_ble_adv_start(0);
        }
    }

    gap_dev_state = new_state;
}

static void
app_ble_gap_handle_conn_state_evt(uint8_t conn_id, T_GAP_CONN_STATE new_state, uint16_t disc_cause)
{
    APP_PRINT_INFO4(
        "app_ble_gap_handle_conn_state_evt: conn_id %d old_state %d new_state %d, disc_cause 0x%x",
        conn_id,
        gap_conn_state,
        new_state,
        disc_cause);
    switch (new_state)
    {
    case GAP_CONN_STATE_DISCONNECTED:
    {
        if ((disc_cause != (HCI_ERR | HCI_ERR_REMOTE_USER_TERMINATE)) &&
            (disc_cause != (HCI_ERR | HCI_ERR_LOCAL_HOST_TERMINATE)))
        {
            APP_PRINT_ERROR1("app_ble_gap_handle_conn_state_evt: connection lost cause 0x%x",
                             disc_cause);
        }
        app_ble_adv_start(0);
    }
    break;

    case GAP_CONN_STATE_CONNECTED:
    {
        uint16_t conn_interval;
        uint16_t conn_latency;
        uint16_t conn_supervision_timeout;
        uint8_t remote_bd[6];
        T_GAP_REMOTE_ADDR_TYPE remote_bd_type;

        le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &conn_interval, conn_id);
        le_get_conn_param(GAP_PARAM_CONN_LATENCY, &conn_latency, conn_id);
        le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &conn_supervision_timeout, conn_id);
        le_get_conn_addr(conn_id, remote_bd, &remote_bd_type);
        APP_PRINT_INFO5("app_ble_gap_handle_conn_state_evt: remote_bd %s, remote_addr_type %d, "
                        "conn_interval 0x%x, conn_latency 0x%x, conn_supervision_timeout 0x%x",
                        TRACE_BDADDR(remote_bd),
                        remote_bd_type,
                        conn_interval,
                        conn_latency,
                        conn_supervision_timeout);
    }
    break;

    default:
        break;
    }
    gap_conn_state = new_state;
}

static void app_ble_gap_handle_authen_state_evt(uint8_t conn_id, uint8_t new_state, uint16_t cause)
{
    APP_PRINT_INFO2("app_ble_gap_handle_authen_state_evt: conn_id %d, cause 0x%x", conn_id, cause);

    switch (new_state)
    {
    case GAP_AUTHEN_STATE_STARTED:
    {
        APP_PRINT_INFO0("app_ble_gap_handle_authen_state_evt: GAP_AUTHEN_STATE_STARTED");
    }
    break;

    case GAP_AUTHEN_STATE_COMPLETE:
    {
        if (cause == GAP_SUCCESS)
        {
#if F_APP_BT_GATT_CLIENT_SUPPORT
            gatt_client_start_discovery_all(le_get_conn_handle(conn_id), NULL);
#endif
            APP_PRINT_INFO0(
                "app_ble_gap_handle_authen_state_evt: GAP_AUTHEN_STATE_COMPLETE pair success");
        }
        else
        {
            APP_PRINT_INFO0(
                "app_ble_gap_handle_authen_state_evt: GAP_AUTHEN_STATE_COMPLETE pair failed");
        }
    }
    break;

    default:
    {
        APP_PRINT_ERROR1("app_ble_gap_handle_authen_state_evt: unknown newstate %d", new_state);
    }
    break;
    }
}

static void app_ble_gap_handle_bond_just_work(uint8_t conn_id, T_GAP_CFM_CAUSE cause)
{
    le_bond_just_work_confirm(conn_id, cause);
    APP_PRINT_INFO1("app_ble_gap_handle_bond_just_work: cause %d", cause);
}

static void app_ble_gap_handle_bond_passkey_display(uint8_t conn_id, T_GAP_CFM_CAUSE cause)
{
    uint32_t display_value = 0;
    le_bond_get_display_key(conn_id, &display_value);
    APP_PRINT_INFO1("app_ble_gap_handle_bond_passkey_display: passkey %d", display_value);
    le_bond_passkey_display_confirm(conn_id, cause);
}

static void app_ble_gap_handle_bond_user_confirmation(uint8_t conn_id, T_GAP_CFM_CAUSE cause)
{
    uint32_t display_value = 0;
    le_bond_get_display_key(conn_id, &display_value);
    APP_PRINT_INFO1("app_ble_gap_handle_bond_user_confirmation: passkey %d", display_value);
    le_bond_user_confirm(conn_id, cause);
}

static void
app_ble_gap_handle_bond_passkey_input(uint8_t conn_id, uint32_t passkey, T_GAP_CFM_CAUSE cause)
{
    APP_PRINT_INFO2(
        "app_ble_gap_handle_bond_passkey_input: conn_id %d, passkey %d", conn_id, passkey);
    le_bond_passkey_input_confirm(conn_id, passkey, cause);
}

static void
app_ble_gap_handle_bond_oob_input(uint8_t conn_id, uint8_t *oob_data, T_GAP_CFM_CAUSE cause)
{
    APP_PRINT_INFO0("app_ble_gap_handle_bond_oob_input");
    le_bond_set_param(GAP_PARAM_BOND_OOB_DATA, GAP_OOB_LEN, oob_data);
    le_bond_oob_input_confirm(conn_id, cause);
}

static void app_ble_gap_handle_conn_mtu_info_evt(uint8_t conn_id, uint16_t mtu_size)
{
    APP_PRINT_INFO2(
        "app_ble_gap_handle_conn_mtu_info_evt: conn_id %d, mtu_size %d", conn_id, mtu_size);
}

static void
app_ble_gap_handle_conn_param_update_evt(uint8_t conn_id, uint8_t status, uint16_t cause)
{
    switch (status)
    {
    case GAP_CONN_PARAM_UPDATE_STATUS_SUCCESS:
    {
        uint16_t conn_interval;
        uint16_t conn_slave_latency;
        uint16_t conn_supervision_timeout;

        le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &conn_interval, conn_id);
        le_get_conn_param(GAP_PARAM_CONN_LATENCY, &conn_slave_latency, conn_id);
        le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &conn_supervision_timeout, conn_id);
        APP_PRINT_INFO3("app_ble_gap_handle_conn_param_update_evt update success: conn_interval "
                        "0x%x, conn_slave_latency 0x%x, conn_supervision_timeout 0x%x",
                        conn_interval,
                        conn_slave_latency,
                        conn_supervision_timeout);
    }
    break;

    case GAP_CONN_PARAM_UPDATE_STATUS_FAIL:
    {
        APP_PRINT_ERROR1("app_ble_gap_handle_conn_param_update_evt update failed: cause 0x%x",
                         cause);
    }
    break;

    case GAP_CONN_PARAM_UPDATE_STATUS_PENDING:
    {
        APP_PRINT_INFO0("app_ble_gap_handle_conn_param_update_evt update pending.");
    }
    break;

    default:
        break;
    }
}

void app_ble_gap_handle_msg(T_IO_MSG *p_gap_msg)
{
    T_LE_GAP_MSG gap_msg;

    memcpy(&gap_msg, &p_gap_msg->u.param, sizeof(p_gap_msg->u.param));
    ble_mgr_handle_gap_msg(p_gap_msg->subtype, &gap_msg);
    APP_PRINT_TRACE1("app_ble_gap_handle_msg: subtype %d", p_gap_msg->subtype);

    switch (p_gap_msg->subtype)
    {
    case GAP_MSG_LE_DEV_STATE_CHANGE:
    {
        app_ble_gap_handle_dev_state_evt(gap_msg.msg_data.gap_dev_state_change.new_state,
                                         gap_msg.msg_data.gap_dev_state_change.cause);
    }
    break;

    case GAP_MSG_LE_CONN_STATE_CHANGE:
    {
        app_ble_gap_handle_conn_state_evt(
            gap_msg.msg_data.gap_conn_state_change.conn_id,
            (T_GAP_CONN_STATE)gap_msg.msg_data.gap_conn_state_change.new_state,
            gap_msg.msg_data.gap_conn_state_change.disc_cause);
    }
    break;

    case GAP_MSG_LE_CONN_MTU_INFO:
    {
        app_ble_gap_handle_conn_mtu_info_evt(gap_msg.msg_data.gap_conn_mtu_info.conn_id,
                                             gap_msg.msg_data.gap_conn_mtu_info.mtu_size);
    }
    break;

    case GAP_MSG_LE_CONN_PARAM_UPDATE:
    {
        app_ble_gap_handle_conn_param_update_evt(gap_msg.msg_data.gap_conn_param_update.conn_id,
                                                 gap_msg.msg_data.gap_conn_param_update.status,
                                                 gap_msg.msg_data.gap_conn_param_update.cause);
    }
    break;

    case GAP_MSG_LE_AUTHEN_STATE_CHANGE:
    {
        app_ble_gap_handle_authen_state_evt(gap_msg.msg_data.gap_authen_state.conn_id,
                                            gap_msg.msg_data.gap_authen_state.new_state,
                                            gap_msg.msg_data.gap_authen_state.status);
    }
    break;

    case GAP_MSG_LE_BOND_JUST_WORK:
    {
        app_ble_gap_handle_bond_just_work(gap_msg.msg_data.gap_bond_just_work_conf.conn_id,
                                          GAP_CFM_CAUSE_ACCEPT);
    }
    break;

    case GAP_MSG_LE_BOND_PASSKEY_DISPLAY:
    {
        app_ble_gap_handle_bond_passkey_display(gap_msg.msg_data.gap_bond_passkey_display.conn_id,
                                                GAP_CFM_CAUSE_ACCEPT);
    }
    break;

    case GAP_MSG_LE_BOND_USER_CONFIRMATION:
    {
        app_ble_gap_handle_bond_user_confirmation(gap_msg.msg_data.gap_bond_user_conf.conn_id,
                                                  GAP_CFM_CAUSE_ACCEPT);
    }
    break;

    case GAP_MSG_LE_BOND_PASSKEY_INPUT:
    {
        uint32_t passkey = 888888;
        app_ble_gap_handle_bond_passkey_input(
            gap_msg.msg_data.gap_bond_passkey_input.conn_id, passkey, GAP_CFM_CAUSE_ACCEPT);
    }
    break;

    case GAP_MSG_LE_BOND_OOB_INPUT:
    {
        uint8_t oob_data[GAP_OOB_LEN] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        app_ble_gap_handle_bond_oob_input(
            gap_msg.msg_data.gap_bond_oob_input.conn_id, oob_data, GAP_CFM_CAUSE_ACCEPT);
    }
    break;

    default:
        break;
    }
}

void app_ble_gap_handle_io_msg(T_IO_MSG io_msg)
{
    uint16_t msg_type = io_msg.type;

    switch (msg_type)
    {
    case IO_MSG_TYPE_BT_STATUS:
    {
        app_ble_gap_handle_msg(&io_msg);
    }
    break;

    default:
        break;
    }
}

T_APP_RESULT app_ble_gap_callback(uint8_t cb_type, void *p_cb_data)
{
    T_APP_RESULT result = APP_RESULT_SUCCESS;
    T_LE_CB_DATA *p_data = (T_LE_CB_DATA *)p_cb_data;
    ble_mgr_handle_gap_cb(cb_type, p_cb_data);
    APP_PRINT_INFO1("app_ble_gap_callback: cb_type 0x%x", cb_type);
    switch (cb_type)
    {
    case GAP_MSG_APP_BOND_MANAGER_INFO:
    {
        result = bt_bond_mgr_handle_gap_msg(p_data->p_le_cb_data);
    }
    break;

    case GAP_MSG_LE_SET_DATA_LEN:
        DBG_DIRECT("BLE set DLE response: conn=%u cause=0x%X",
                   p_data->p_le_set_data_len_rsp->conn_id,
                   p_data->p_le_set_data_len_rsp->cause);
        break;

    case GAP_MSG_LE_DATA_LEN_CHANGE_INFO:
        DBG_DIRECT("BLE DLE: conn=%u tx_octets=%u tx_time=%u "
                   "rx_octets=%u rx_time=%u",
                   p_data->p_le_data_len_change_info->conn_id,
                   p_data->p_le_data_len_change_info->max_tx_octets,
                   p_data->p_le_data_len_change_info->max_tx_time,
                   p_data->p_le_data_len_change_info->max_rx_octets,
                   p_data->p_le_data_len_change_info->max_rx_time);
        break;

    case GAP_MSG_LE_PHY_UPDATE_INFO:
        DBG_DIRECT("BLE PHY: conn=%u cause=0x%X tx=%u rx=%u",
                   p_data->p_le_phy_update_info->conn_id,
                   p_data->p_le_phy_update_info->cause,
                   p_data->p_le_phy_update_info->tx_phy,
                   p_data->p_le_phy_update_info->rx_phy);
        break;

    case GAP_MSG_LE_MODIFY_WHITE_LIST:
        APP_PRINT_INFO2(
            "app_ble_gap_callback: GAP_MSG_LE_MODIFY_WHITE_LIST operation %d, cause 0x%x",
            p_data->p_le_modify_white_list_rsp->operation,
            p_data->p_le_modify_white_list_rsp->cause);
        break;

    default:
        break;
    }
    return result;
}
