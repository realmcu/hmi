/*
 * Compat shim for the OTA port: the subset of app_ble_gap the OTA code uses.
 */
#ifndef _APP_OTA_COMPAT_APP_BLE_GAP_H_
#define _APP_OTA_COMPAT_APP_BLE_GAP_H_

#include <stdbool.h>
#include "app_link_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local disconnect reasons the OTA flow distinguishes in app_ota_le_disconnect_cb(). */
typedef enum
{
    LE_LOCAL_DISC_CAUSE_UNKNOWN = 0,
    LE_LOCAL_DISC_CAUSE_OTA_RESET,
    LE_LOCAL_DISC_CAUSE_SWITCH_TO_OTA,
} T_LE_LOCAL_DISC_CAUSE;

/* Disconnect the given LE link; the local cause is remembered so it is delivered to the
 * link's disc_callback when the disconnect completes. */
bool app_ble_gap_disconnect(T_APP_LE_LINK *p_link, T_LE_LOCAL_DISC_CAUSE disc_cause);

#ifdef __cplusplus
}
#endif
#endif /* _APP_OTA_COMPAT_APP_BLE_GAP_H_ */
