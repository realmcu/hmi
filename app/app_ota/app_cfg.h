/*
 * Compat shim for the OTA port: minimal app_cfg_nv used by ota_service.c
 * (only bud_local_addr, for the OTA MAC-address read characteristic).
 */
#ifndef _APP_OTA_COMPAT_APP_CFG_H_
#define _APP_OTA_COMPAT_APP_CFG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t bud_local_addr[6];
} T_APP_CFG_NV;

extern T_APP_CFG_NV app_cfg_nv;

#ifdef __cplusplus
}
#endif
#endif /* _APP_OTA_COMPAT_APP_CFG_H_ */
