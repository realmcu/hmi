/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <bt_task.h>
#include "bt_gatt_svc.h"
#include "trace.h"
#include "app_ble/app_ble_hmi.h"
#include "app_ble/app_ble_nus.h"

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
        APP_PRINT_INFO2("app_gatt_svc_general_cb: GATT_SVC_EVENT_REG_AFTER_INIT_RESULT service_id %d, cause 0x%x",
                        p_result->service_id, p_result->cause);
    }
}

void app_le_profile_init(void)
{
    gatt_svc_init(GATT_SVC_USE_EXT_SERVER, 2);
    gatt_svc_register_general_cb(app_gatt_svc_general_cb);
    app_ble_hmi_init();
    app_ble_nus_init();
}
