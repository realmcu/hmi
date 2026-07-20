/*
 * OTA service registration + app bridge for eBadge_8773g.
 *
 * The DFU packet / control-point writes are dispatched to app_ota directly inside
 * ota_service_attr_write_cb() (app_ota_ble_handle_packet / app_ota_ble_handle_cp_req).
 * This general callback only receives the "OTA enter" char write and service events,
 * mirroring the sample's app_ble_service_ota_srv_cb().
 */
#include <stdint.h>
#include <bt_types.h>
#include "trace.h"
#include "ota_service.h"
#include "app_ota.h"
#include "app_ota_service.h"

static T_APP_RESULT app_ble_service_ota_srv_cb(T_SERVER_ID service_id, void *p_data)
{
    T_OTA_CALLBACK_DATA *p_ota_cb = (T_OTA_CALLBACK_DATA *)p_data;

    APP_PRINT_INFO2("app_ble_service_ota_srv_cb: service_id %d, msg_type %d",
                    service_id, p_ota_cb->msg_type);

    return APP_RESULT_SUCCESS;
}

void app_ota_service_init(void)
{
    app_ota_init();                                     /* register OTA timer callback */
    ota_add_service((void *)app_ble_service_ota_srv_cb); /* add the DFU GATT service */
}
