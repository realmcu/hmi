#include <string.h>
#include "app_ble_nus.h"
#include "trace.h"
#include "ble_gap_msg.h"
#include "gap_msg.h"
#include "bt_gatt_svc.h"
#include "gap_conn_le.h"
#include "nordic_uart_service.h"

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
                APP_PRINT_INFO0("app_nus_callback: NUS_NOTIFY_TX_ENABLE");
            }
            else
            {
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

void app_ble_nus_init(void)
{
    nus_service_add_service(app_nus_callback);
    le_msg_handler_cback_register(gap_nus_msg);
}
