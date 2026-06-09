/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include "hmi_bt_task.h"
#include "bt_gatt_svc.h"
#include "trace.h"
#include "hmi_ble_ctrl.h"
#include "hmi_ble_nus.h"
#include "ota_service.h"
#include "gap_conn_le.h"

static T_APP_RESULT ota_srv_cb(T_SERVER_ID service_id, void *p_data)
{
    T_OTA_CALLBACK_DATA *p_ota_cb_data = (T_OTA_CALLBACK_DATA *)p_data;
    if (p_ota_cb_data->msg_type == SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE &&
        p_ota_cb_data->msg_data.write.opcode == OTA_WRITE_CHAR_VAL &&
        p_ota_cb_data->msg_data.write.value == OTA_VALUE_ENTER)
    {
        APP_PRINT_INFO1("ota_srv_cb: switch to OTA mode, conn_id %d", p_ota_cb_data->conn_id);
        le_disconnect(p_ota_cb_data->conn_id);
    }
    return APP_RESULT_SUCCESS;
}

static void app_gatt_svc_general_cb(uint8_t type, void *p_data)
{
    if (type == GATT_SVC_EVENT_REG_RESULT)
    {
        T_GATT_SVC_REG_RESULT *p_result = (T_GATT_SVC_REG_RESULT *)p_data;
        APP_PRINT_INFO1("app_gatt_svc_general_cb: GATT_SVC_EVENT_REG_RESULT result 0x%x",
                        p_result->result);
    }
    else if (type == GATT_SVC_EVENT_REG_AFTER_INIT_RESULT)
    {
        T_GATT_SVC_REG_AFTER_INIT_RESULT *p_result = (T_GATT_SVC_REG_AFTER_INIT_RESULT *)p_data;
        APP_PRINT_INFO2("app_gatt_svc_general_cb: GATT_SVC_EVENT_REG_AFTER_INIT_RESULT "
                        "service_id %d, cause 0x%x",
                        p_result->service_id, p_result->cause);
    }
}

void hmi_ble_profile_init(void)
{
    gatt_svc_init(GATT_SVC_USE_EXT_SERVER, 3);
    gatt_svc_register_general_cb(app_gatt_svc_general_cb);
    hmi_ble_ctrl_init();
    hmi_ble_nus_init();
    ota_add_service(ota_srv_cb);
}
