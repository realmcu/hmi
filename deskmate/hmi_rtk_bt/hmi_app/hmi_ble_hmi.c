#include <string.h>
#include "hmi_ble_hmi.h"
#include "trace.h"
#include "hmi_ble_gap_msg.h"
#include "gap_msg.h"
#include "bt_gatt_svc.h"
#include "gap_conn_le.h"
#include "hmi_private_service.h"

static void app_hmi_send_data_cb(T_EXT_SEND_DATA_RESULT result)
{
    if (result.cause == GAP_SUCCESS)
    {
        APP_PRINT_INFO2("app_hmi_send_data_cb: notify sent ok, conn_handle 0x%x, credits %d",
                        result.conn_handle, result.credits);
    }
    else
    {
        APP_PRINT_ERROR2("app_hmi_send_data_cb: notify sent fail, conn_handle 0x%x, cause 0x%x",
                         result.conn_handle, result.cause);
    }
}

static T_APP_RESULT app_hmi_callback(T_SERVER_ID service_id, void *p_data)
{
    T_APP_RESULT app_result = APP_RESULT_SUCCESS;

    if (service_id == hmi_service_id)
    {
        T_HMI_CALLBACK_DATA *p_hmi_cb = (T_HMI_CALLBACK_DATA *)p_data;
        switch (p_hmi_cb->msg_type)
        {
        case SERVICE_CALLBACK_TYPE_INDIFICATION_NOTIFICATION:
            if (p_hmi_cb->msg_data.notify_index == HMI_NOTIFY_EVENT_ENABLE)
            {
                APP_PRINT_INFO0("app_hmi_callback: HMI_NOTIFY_EVENT_ENABLE");
            }
            else
            {
                APP_PRINT_INFO0("app_hmi_callback: HMI_NOTIFY_EVENT_DISABLE");
            }
            break;

        case SERVICE_CALLBACK_TYPE_READ_CHAR_VALUE:
            APP_PRINT_INFO0("app_hmi_callback: HMI STATUS read");
            break;

        case SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE:
            APP_PRINT_INFO3("app_hmi_callback: HMI CMD write, type %d, len %d, data %b",
                            p_hmi_cb->msg_data.write.write_type,
                            p_hmi_cb->msg_data.write.len,
                            TRACE_BINARY(p_hmi_cb->msg_data.write.len, p_hmi_cb->msg_data.write.p_value));
            break;

        default:
            break;
        }
    }

    return app_result;
}

static void gap_hmi_msg(T_IO_MSG *p_gap_msg)
{
    APP_PRINT_TRACE2("gap_hmi_msg: type %d, subtype %d", p_gap_msg->type, p_gap_msg->subtype);
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
                APP_PRINT_INFO0("gap_hmi_msg: disconnected");
                break;

            case GAP_CONN_STATE_CONNECTED:
                APP_PRINT_INFO0("gap_hmi_msg: connected");
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

void hmi_ble_hmi_init(void)
{
    hmi_service_add_service(app_hmi_callback, app_hmi_send_data_cb);
    hmi_le_msg_cback_register(gap_hmi_msg);
}
