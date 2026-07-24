/*
 * Entry points for the ported BLE OTA (DFU) module in eBadge_8773g.
 *  - app_ota_service_init(): register the DFU GATT service + init app_ota. Call once,
 *    after gatt_svc_init(), e.g. from hmi_ble_profile_init().
 *  - app_ota_glue_link_connected/disconnected(): feed eBadge's GAP connect/disconnect
 *    events into the OTA link table (needed so the OTA reset-after-transfer works).
 */
#ifndef _APP_OTA_SERVICE_H_
#define _APP_OTA_SERVICE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_ota_service_init(void);

void app_ota_glue_link_connected(uint8_t conn_id, uint16_t conn_handle, uint8_t *bd_addr);
void app_ota_glue_link_disconnected(uint8_t conn_id, uint16_t disc_cause);

#ifdef __cplusplus
}
#endif
#endif /* _APP_OTA_SERVICE_H_ */
