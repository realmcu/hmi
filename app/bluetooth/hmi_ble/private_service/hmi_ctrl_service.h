#ifndef _HMI_PRIVATE_SERVICE_H_
#define _HMI_PRIVATE_SERVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <profile_server_ext.h>
#include <bt_gatt_svc.h>

/*============================================================================*
 *                              Macros
 *============================================================================*/

/* 128-bit Service UUID */
extern const uint8_t GATT_UUID128_HMI_SERVICE[16];

/* Characteristic UUIDs (16-bit, vendor specific).
 * NOTE: kept in the 0xFFCx range, NOT 0xFFDx.  The Realtek OTA/DFU service
 * occupies 0xFFD0~0xFFD4 (app/ota/ota_service.h: OTA=0xFFD1, MAC=0xFFD2,
 * PATCH=0xFFD3, APP_VER=0xFFD4).  Overlapping there makes a BLE client resolve
 * these characteristics to the OTA copy and mis-route writes -- do not move. */
#define BLE_UUID_HMI_CMD                0xFFC1  /* Write: peer -> display */
#define BLE_UUID_HMI_EVENT              0xFFC2  /* Notify: display -> peer */
#define BLE_UUID_HMI_STATUS             0xFFC3  /* Read: display status   */

#define HMI_CMD_MAX_LEN                 512
#define HMI_STATUS_MAX_LEN              32

/* T_HMI_PARAM_TYPE */
typedef enum
{
    HMI_SERVICE_PARAM_STATUS = 0x01,
} T_HMI_PARAM_TYPE;

/* Read value index */
#define HMI_READ_STATUS                 1

/* Write opcode */
#define HMI_WRITE_CMD                   1

/* Notify enable/disable */
#define HMI_NOTIFY_EVENT_ENABLE         1
#define HMI_NOTIFY_EVENT_DISABLE        2

/*============================================================================*
 *                              Types
 *============================================================================*/

typedef struct
{
    uint8_t          opcode;
    T_WRITE_TYPE     write_type;
    uint16_t         len;
    uint8_t          *p_value;
} T_HMI_WRITE_MSG;

typedef union
{
    uint8_t          notify_index;
    uint8_t          read_value_index;
    T_HMI_WRITE_MSG  write;
} T_HMI_UPSTREAM_MSG_DATA;

typedef struct
{
    uint16_t                 conn_handle;
    uint16_t                 cid;
    uint8_t                  conn_id;
    T_SERVICE_CALLBACK_TYPE  msg_type;
    T_HMI_UPSTREAM_MSG_DATA  msg_data;
} T_HMI_CALLBACK_DATA;

/*============================================================================*
 *                              Functions
 *============================================================================*/

extern T_SERVER_ID hmi_ctrl_service_id;

T_SERVER_ID hmi_ctrl_service_add_service(void *p_func, P_FUN_GATT_EXT_SEND_DATA_CB send_cb);
bool        hmi_ctrl_service_set_parameter(T_HMI_PARAM_TYPE param_type, uint16_t len,
                                           void *p_value);
bool        hmi_ctrl_service_notify(uint16_t conn_handle, void *p_value, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PRIVATE_SERVICE_H_ */
