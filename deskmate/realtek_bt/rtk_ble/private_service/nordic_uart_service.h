#ifndef _NORDIC_UART_SERVICE_H_
#define _NORDIC_UART_SERVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <profile_server_ext.h>
#include <stdbool.h>
#include <stdint.h>

/*============================================================================*
 *                              Macros
 *============================================================================*/

/* NUS UUIDs */
extern const uint8_t GATT_UUID128_NUS_SERVICE[16];
extern const uint8_t GATT_UUID128_NUS_RX_CHAR[16];
extern const uint8_t GATT_UUID128_NUS_TX_CHAR[16];

#define NUS_RX_MAX_LEN                  512
#define NUS_TX_MAX_LEN                  512

/* Read/Write opcode */
#define NUS_WRITE_RX                    1

/* Notify enable/disable */
#define NUS_NOTIFY_TX_ENABLE            1
#define NUS_NOTIFY_TX_DISABLE           2

/*============================================================================*
 *                              Types
 *============================================================================*/

typedef struct
{
    uint8_t          opcode;
    T_WRITE_TYPE     write_type;
    uint16_t         len;
    uint8_t          *p_value;
} T_NUS_WRITE_MSG;

typedef union
{
    uint8_t          notify_index;
    T_NUS_WRITE_MSG  write;
} T_NUS_UPSTREAM_MSG_DATA;

typedef struct
{
    uint16_t                 conn_handle;
    uint16_t                 cid;
    uint8_t                  conn_id;
    T_SERVICE_CALLBACK_TYPE  msg_type;
    T_NUS_UPSTREAM_MSG_DATA  msg_data;
} T_NUS_CALLBACK_DATA;

/*============================================================================*
 *                              Functions
 *============================================================================*/

extern T_SERVER_ID nus_service_id;

T_SERVER_ID nus_service_add_service(void *p_func);

bool nus_service_send_data(uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id,
                           void *p_value, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* _NORDIC_UART_SERVICE_H_ */
