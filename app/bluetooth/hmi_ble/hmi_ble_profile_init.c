/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include "hmi_bt_task.h"
#include "bt_gatt_svc.h"
#include "trace.h"
#if 0 /* V1.2 migration: hmi_ble_ctrl.c parked; replaced by app/protocol/port/ebadge_port_ble.c */
#include "hmi_ble_ctrl.h"
#endif
#include "hmi_ble_nus.h"
#include "app_ota_service.h"
#include "hmi_ctrl_service.h"

/* V1.2 migration: the HMI CTRL service (128-bit UUID f48affc0..) is now
 * registered here alongside NUS and OTA, in the correct BT-stack init order
 * (right after gatt_svc_init()).  The two callbacks live in
 * app/protocol/port/ebadge_port_ble.c and are re-exposed non-static via the
 * ebadge_port_ble__ prefix -- see the comment on ebadge_port_ble_init(). */
extern T_APP_RESULT ebadge_port_ble__on_hmi_service_cb(T_SERVER_ID service_id, void *p_data);
extern void         ebadge_port_ble__on_send_data_cb(T_EXT_SEND_DATA_RESULT result);

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
    /* Ext-server service count: HMI ctrl (128-bit) + NUS + OTA/DFU = 3.
     * Stream service was removed in the V1.2 migration -- data plane is
     * raw TCP over SoftAP now (see eBadge-PROT-001 §5). */
    gatt_svc_init(GATT_SVC_USE_EXT_SERVER, 3);
    gatt_svc_register_general_cb(app_gatt_svc_general_cb);
    /* V1.2 migration: HMI CTRL service registered here (post-gatt_svc_init).
     * Callbacks route WRITE_CHAR_VALUE / CCCD updates / TX-complete back
     * into ebadge_port_ble.c which drives ebadge_task's l2_task. */
    hmi_ctrl_service_add_service((void *)ebadge_port_ble__on_hmi_service_cb,
                                 ebadge_port_ble__on_send_data_cb);
    hmi_ble_nus_init();
    app_ota_service_init();                       /* register BLE OTA/DFU service + app_ota_init() */
}
