/*
 * Compat shim for the OTA port. The SPP / vendor-command OTA path
 * (app_ota_cmd_handle / app_ota_cmd_ack_handle) is compiled out for this
 * BLE-DFU-only port (APP_OTA_SPP_SUPPORT=0), so no command/event enum
 * definitions are needed here. Intentionally empty.
 */
#ifndef _APP_OTA_COMPAT_APP_CMD_H_
#define _APP_OTA_COMPAT_APP_CMD_H_
#endif /* _APP_OTA_COMPAT_APP_CMD_H_ */
