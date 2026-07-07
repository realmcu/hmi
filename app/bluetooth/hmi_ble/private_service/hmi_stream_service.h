#ifndef _HMI_STREAM_SERVICE_H_
#define _HMI_STREAM_SERVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <profile_server_ext.h>
#include <bt_gatt_svc.h>

/*============================================================================*
 *                              Macros
 *============================================================================*/

/* 128-bit Stream Service UUID: 484D5354-0000-1000-8000-00805F9B34FB ("HMST",
 * same 484D ("HM") prefix as the control service "HMIS" 484D4953). */
extern const uint8_t GATT_UUID128_HMI_STREAM_SERVICE[16];

/* Characteristic UUIDs (16-bit, vendor specific; continue 0xFFD1~0xFFD3) */
#define BLE_UUID_HMI_STREAM_RX          0xFFD4  /* Write(no rsp): peer -> device, KS_FRAME      */
#define BLE_UUID_HMI_STREAM_TX          0xFFD5  /* Notify: device -> peer, KS_ACK/CREDIT/REPORT */

/*============================================================================*
 *                              Types
 *============================================================================*/

/* RX sink: invoked in BLE callback context when the peer writes to 0xFFD4.
 * The data is the raw L2 message (no L1 wrapper): [CMD_STREAM][ver][KV...]. */
typedef void (*hmi_stream_rx_cb_t)(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/* CCCD callback: peer enabled/disabled notifications on 0xFFD5. */
typedef void (*hmi_stream_cccd_cb_t)(uint16_t conn_handle, bool notify_enabled);

/*============================================================================*
 *                              Functions
 *============================================================================*/

extern T_SERVER_ID hmi_stream_service_id;

/* Register the independent stream GATT service.  rx_cb is called on every
 * write to the RX characteristic; cccd_cb on notify enable/disable. */
T_SERVER_ID hmi_stream_service_add_service(hmi_stream_rx_cb_t rx_cb,
                                           hmi_stream_cccd_cb_t cccd_cb);

/* Send a notification (KS_ACK/KS_CREDIT/KS_REPORT) to the peer on 0xFFD5.
 * Fire-and-forget: returns the GATT submit result, does not wait for completion. */
bool hmi_stream_service_notify(uint16_t conn_handle, const void *p_value, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_STREAM_SERVICE_H_ */
