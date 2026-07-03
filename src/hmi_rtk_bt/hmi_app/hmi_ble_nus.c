/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include "hmi_ble_nus.h"
#include "trace.h"
#include "hmi_ble_gap_msg.h"
#include "gap_msg.h"
#include "gap_conn_le.h"
#include "nordic_uart_service.h"

static bool nus_tx_cccd_enabled = false;

static void app_nus_send_data_cb(T_EXT_SEND_DATA_RESULT result)
{
    if (result.cause == GAP_SUCCESS)
    {
        APP_PRINT_INFO2("app_nus_send_data_cb: notify sent ok, conn_handle 0x%x, credits %d",
                        result.conn_handle, result.credits);
    }
    else
    {
        APP_PRINT_ERROR2("app_nus_send_data_cb: notify sent fail, conn_handle 0x%x, cause 0x%x",
                         result.conn_handle, result.cause);
    }
}

static T_APP_RESULT app_nus_callback(T_SERVER_ID service_id, void *p_data)
{
    T_APP_RESULT app_result = APP_RESULT_SUCCESS;

    if (service_id == nus_service_id)
    {
        T_NUS_CALLBACK_DATA *p_nus_cb = (T_NUS_CALLBACK_DATA *)p_data;
        switch (p_nus_cb->msg_type)
        {
        case SERVICE_CALLBACK_TYPE_INDIFICATION_NOTIFICATION:
            if (p_nus_cb->msg_data.notify_index == NUS_NOTIFY_TX_ENABLE)
            {
                nus_tx_cccd_enabled = true;
                APP_PRINT_INFO0("app_nus_callback: NUS_NOTIFY_TX_ENABLE");
            }
            else
            {
                nus_tx_cccd_enabled = false;
                APP_PRINT_INFO0("app_nus_callback: NUS_NOTIFY_TX_DISABLE");
            }
            break;

        case SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE:
            APP_PRINT_INFO3("app_nus_callback: NUS RX write, type %d, len %d, data %b",
                            p_nus_cb->msg_data.write.write_type,
                            p_nus_cb->msg_data.write.len,
                            TRACE_BINARY(p_nus_cb->msg_data.write.len,
                                         p_nus_cb->msg_data.write.p_value));
            break;

        default:
            break;
        }
    }

    return app_result;
}

static void gap_nus_msg(T_IO_MSG *p_gap_msg)
{
    APP_PRINT_TRACE2("gap_nus_msg: type %d, subtype %d", p_gap_msg->type, p_gap_msg->subtype);
    T_LE_GAP_MSG gap_msg;
    memcpy(&gap_msg, &p_gap_msg->u.param, sizeof(p_gap_msg->u.param));

    switch (p_gap_msg->type)
    {
    case IO_MSG_TYPE_BT_STATUS:
        switch (p_gap_msg->subtype)
        {
        case GAP_MSG_LE_CONN_STATE_CHANGE:
            switch ((T_GAP_CONN_STATE)gap_msg.msg_data.gap_conn_state_change.new_state)
            {
            case GAP_CONN_STATE_DISCONNECTED:
                nus_tx_cccd_enabled = false;
                APP_PRINT_INFO0("gap_nus_msg: disconnected");
                break;

            case GAP_CONN_STATE_CONNECTED:
                APP_PRINT_INFO0("gap_nus_msg: connected");
                break;

            default:
                break;
            }
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

void hmi_ble_nus_init(void)
{
    nus_service_add_service(app_nus_callback, app_nus_send_data_cb);
    hmi_le_msg_cback_register(gap_nus_msg);
}
