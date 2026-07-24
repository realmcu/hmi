/*
 * Compat shim for the OTA port: minimal LE link table used by app_ota.c / ota_service.c.
 * Backed by app_ota_glue.c (fed from eBadge's GAP connect/disconnect events).
 */
#ifndef _APP_OTA_COMPAT_APP_LINK_UTIL_H_
#define _APP_OTA_COMPAT_APP_LINK_UTIL_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_BLE_LINK_NUM
#define MAX_BLE_LINK_NUM 4
#endif

typedef void (*P_FUN_LE_LINK_DISC_CB)(uint8_t conn_id, uint8_t local_disc_cause,
                                      uint16_t disc_cause);

typedef struct
{
    uint8_t  used;
    uint8_t  conn_id;
    uint16_t conn_handle;
    uint8_t  bd_addr[6];
    uint8_t  local_disc_cause;      /* set by app_ble_gap_disconnect(), used on disconnect */
    P_FUN_LE_LINK_DISC_CB disc_callback;
} T_APP_LE_LINK;

T_APP_LE_LINK *app_link_find_le_link_by_conn_id(uint8_t conn_id);
bool app_link_reg_le_link_disc_cb(uint8_t conn_id, P_FUN_LE_LINK_DISC_CB p_fun_cb);

#ifdef __cplusplus
}
#endif
#endif /* _APP_OTA_COMPAT_APP_LINK_UTIL_H_ */
