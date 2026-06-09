/*
 * app_report.c — event reporting stub.
 *
 * BLE OTA responses go via ota_service_send_notification() called inside
 * app_ota_ble_handle_cp_req(); SPP is not supported in hmi_dashboard, so the
 * BR/EDR path of app_report_event is unused.
 */

#include "trace.h"
#include "app_report.h"

void app_report_event(uint8_t cmd_path, uint16_t event_id, uint8_t app_index,
                      uint8_t *data, uint16_t len)
{
    APP_PRINT_TRACE4("app_report_event: cmd_path %d, event_id 0x%04x, app_index %d, len %d",
                     cmd_path, event_id, app_index, len);
}
