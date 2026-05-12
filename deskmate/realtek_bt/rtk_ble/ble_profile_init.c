/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <bt_task.h>
#include "profile_client.h"
#include <profile_server_ext.h>
#include "bt_gatt_svc.h"
#include "gap_conn_le.h"
#include "trace.h"
#include "hmi_private_service.h"

T_SERVER_ID hmi_srv_id;

T_APP_RESULT app_profile_callback(T_SERVER_ID service_id, void *p_data)
{
    T_APP_RESULT app_result = APP_RESULT_SUCCESS;
    uint8_t conn_id = 0xFF;

    if (service_id == SERVICE_PROFILE_GENERAL_ID)
    {
        T_SERVER_EXT_APP_CB_DATA *p_param = (T_SERVER_EXT_APP_CB_DATA *)p_data;

        switch (p_param->eventId)
        {
        case PROFILE_EVT_SRV_REG_COMPLETE:
            APP_PRINT_INFO1("app_profile_callback: PROFILE_EVT_SRV_REG_COMPLETE result %d",
                            p_param->event_data.service_reg_result);
            break;

        case PROFILE_EVT_SEND_DATA_COMPLETE:
            le_get_conn_id_by_handle(p_param->event_data.send_data_result.conn_handle, &conn_id);
            APP_PRINT_INFO5("app_profile_callback: PROFILE_EVT_SEND_DATA_COMPLETE conn_id %d, cause 0x%x, service_id %d, attrib_idx 0x%x, credits %d",
                            conn_id,
                            p_param->event_data.send_data_result.cause,
                            p_param->event_data.send_data_result.service_id,
                            p_param->event_data.send_data_result.attrib_idx,
                            p_param->event_data.send_data_result.credits);
            if (p_param->event_data.send_data_result.cause == GAP_SUCCESS)
            {
                APP_PRINT_INFO0("app_profile_callback: PROFILE_EVT_SEND_DATA_COMPLETE success");
            }
            else
            {
                APP_PRINT_ERROR0("app_profile_callback: PROFILE_EVT_SEND_DATA_COMPLETE failed");
            }
            if (!gatt_svc_handle_profile_data_cmpl(p_param->event_data.send_data_result.conn_handle,
                                                   p_param->event_data.send_data_result.cid,
                                                   p_param->event_data.send_data_result.service_id,
                                                   p_param->event_data.send_data_result.attrib_idx,
                                                   p_param->event_data.send_data_result.credits,
                                                   p_param->event_data.send_data_result.cause))
            {
                APP_PRINT_ERROR0("app_profile_callback: gatt_svc_handle_profile_data_cmpl failed");
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
                APP_PRINT_INFO0("app_profile_callback: HMI_NOTIFY_EVENT_ENABLE");
            }
            else
            {
                APP_PRINT_INFO0("app_profile_callback: HMI_NOTIFY_EVENT_DISABLE");
            }
            break;

        case SERVICE_CALLBACK_TYPE_READ_CHAR_VALUE:
            APP_PRINT_INFO0("app_profile_callback: HMI STATUS read");
            break;

        case SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE:
            APP_PRINT_INFO2("app_profile_callback: HMI CMD write, type %d, len %d",
                            p_hmi_cb->msg_data.write.write_type,
                            p_hmi_cb->msg_data.write.len);
            break;

        default:
            break;
        }
    }

    return app_result;
}

void app_le_profile_init(void)
{
    server_cfg_use_ext_api(true);
    server_ext_register_app_cb(app_profile_callback);
    gatt_svc_init(GATT_SVC_USE_EXT_SERVER, 1);

    hmi_srv_id = hmi_service_add_service(app_profile_callback);
}
