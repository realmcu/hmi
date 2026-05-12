#include <string.h>
#include "app_ble_hmi.h"
#include "trace.h"
#include "ble_gap_msg.h"
#include "gap_msg.h"
#include "bt_gatt_svc.h"
#include "gap_conn_le.h"
#include "hmi_private_service.h"

T_SERVER_ID hmi_srv_id;

static T_APP_RESULT app_hmi_callback(T_SERVER_ID service_id, void *p_data)
{
    T_APP_RESULT app_result = APP_RESULT_SUCCESS;
    uint8_t conn_id = 0xFF;

    if (service_id == SERVICE_PROFILE_GENERAL_ID)
    {
        T_SERVER_EXT_APP_CB_DATA *p_param = (T_SERVER_EXT_APP_CB_DATA *)p_data;
        switch (p_param->eventId)
        {
        case PROFILE_EVT_SRV_REG_COMPLETE:
            APP_PRINT_INFO1("app_hmi_callback: PROFILE_EVT_SRV_REG_COMPLETE result %d",
                            p_param->event_data.service_reg_result);
            break;

        case PROFILE_EVT_SEND_DATA_COMPLETE:
            le_get_conn_id_by_handle(p_param->event_data.send_data_result.conn_handle, &conn_id);
            APP_PRINT_INFO5("app_hmi_callback: PROFILE_EVT_SEND_DATA_COMPLETE conn_id %d, cause 0x%x, service_id %d, attrib_idx 0x%x, credits %d",
                            conn_id,
                            p_param->event_data.send_data_result.cause,
                            p_param->event_data.send_data_result.service_id,
                            p_param->event_data.send_data_result.attrib_idx,
                            p_param->event_data.send_data_result.credits);
            if (p_param->event_data.send_data_result.cause == GAP_SUCCESS)
            {
                APP_PRINT_INFO0("app_hmi_callback: PROFILE_EVT_SEND_DATA_COMPLETE success");
            }
            else
            {
                APP_PRINT_ERROR0("app_hmi_callback: PROFILE_EVT_SEND_DATA_COMPLETE failed");
            }
            if (!gatt_svc_handle_profile_data_cmpl(p_param->event_data.send_data_result.conn_handle,
                                                   p_param->event_data.send_data_result.cid,
                                                   p_param->event_data.send_data_result.service_id,
                                                   p_param->event_data.send_data_result.attrib_idx,
                                                   p_param->event_data.send_data_result.credits,
                                                   p_param->event_data.send_data_result.cause))
            {
                APP_PRINT_ERROR0("app_hmi_callback: gatt_svc_handle_profile_data_cmpl failed");
            }
            break;

        default:
            break;
        }
    }
    else if (service_id == hmi_srv_id)
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

void app_ble_hmi_init(void)
{
    hmi_srv_id = hmi_service_add_service(app_hmi_callback);
    le_msg_handler_cback_register(gap_hmi_msg);
}
