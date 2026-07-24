/*
 * Compat shim for the OTA port: minimal app_db used by app_ota.c / ota_service.c.
 * (Full sample app_main.h is a whole-app structure; the OTA code only touches
 *  factory_addr and le_link[], so we provide just those.)
 */
#ifndef _APP_OTA_COMPAT_APP_MAIN_H_
#define _APP_OTA_COMPAT_APP_MAIN_H_

#include <stdint.h>
#include "app_link_util.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t       factory_addr[6];              /* device MAC reported over the OTA MAC char */
    T_APP_LE_LINK le_link[MAX_BLE_LINK_NUM];
} T_APP_DB;

extern T_APP_DB app_db;

#ifdef __cplusplus
}
#endif
#endif /* _APP_OTA_COMPAT_APP_MAIN_H_ */
