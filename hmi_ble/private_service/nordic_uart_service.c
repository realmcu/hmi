#include <string.h>
#include <trace.h>
#include <gap.h>
#include "gap_conn_le.h"
#include "bt_gatt_svc.h"
#include "bt_types.h"
#include "nordic_uart_service.h"

/*============================================================================*
 *                              Macros
 *============================================================================*/

typedef enum
{
    NUS_SVC_IDX_PRIMARY_SVC,            /* 0 */
    NUS_SVC_IDX_RX_CHAR,               /* 1 */
    NUS_SVC_IDX_RX_VAL,                /* 2 - NUS_SVC_CHAR_RX_WRITE_INDEX */
    NUS_SVC_IDX_RX_USER_DESC,          /* 3 */
    NUS_SVC_IDX_TX_CHAR,               /* 4 */
    NUS_SVC_IDX_TX_VAL,                /* 5 - NUS_SVC_CHAR_TX_NOTIFY_INDEX */
    NUS_SVC_IDX_TX_CCCD,               /* 6 - NUS_SVC_CHAR_TX_CCCD_INDEX */
    NUS_SVC_IDX_TX_USER_DESC,          /* 7 */
} T_NUS_SVC_IDX;

#define NUS_SVC_CHAR_RX_WRITE_INDEX         NUS_SVC_IDX_RX_VAL
#define NUS_SVC_CHAR_TX_NOTIFY_INDEX        NUS_SVC_IDX_TX_VAL
#define NUS_SVC_CHAR_TX_CCCD_INDEX          NUS_SVC_IDX_TX_CCCD

/*============================================================================*
 *                              Local Variables
 *============================================================================*/

T_SERVER_ID nus_service_id;

static P_FUN_EXT_SERVER_GENERAL_CB pfn_nus_service_cb = NULL;

static const uint8_t nus_rx_user_desc[] = "NUS RX";
static const uint8_t nus_tx_user_desc[] = "NUS TX";

/* 128-bit Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E */
const uint8_t GATT_UUID128_NUS_SERVICE[16] =
{
    0x9E, 0xCA, 0xDC, 0x24,
    0x0E, 0xE5,
    0xA9, 0xE0,
    0x93, 0xF3,
    0xA3, 0xB5,
    0x01, 0x00, 0x40, 0x6E
};

/* 128-bit RX Char UUID: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E */
const uint8_t GATT_UUID128_NUS_RX_CHAR[16] =
{
    0x9E, 0xCA, 0xDC, 0x24,
    0x0E, 0xE5,
    0xA9, 0xE0,
    0x93, 0xF3,
    0xA3, 0xB5,
    0x02, 0x00, 0x40, 0x6E
};

/* 128-bit TX Char UUID: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E */
const uint8_t GATT_UUID128_NUS_TX_CHAR[16] =
{
    0x9E, 0xCA, 0xDC, 0x24,
    0x0E, 0xE5,
    0xA9, 0xE0,
    0x93, 0xF3,
    0xA3, 0xB5,
    0x03, 0x00, 0x40, 0x6E
};

/* Inline UUID macros for GATT table type_value field */
#define NUS_UUID_RX_CHAR_BYTES \
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, \
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
#define NUS_UUID_TX_CHAR_BYTES \
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, \
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E

/*============================================================================*
 *                              GATT Service Table
 *============================================================================*/

const T_ATTRIB_APPL nus_service_tbl[] =
{
    /* <<Primary Service>>, index 0 */
    {
        (ATTRIB_FLAG_VOID | ATTRIB_FLAG_LE),
        {
            LO_WORD(GATT_UUID_PRIMARY_SERVICE),
            HI_WORD(GATT_UUID_PRIMARY_SERVICE),
        },
        UUID_128BIT_SIZE,
        (void *)GATT_UUID128_NUS_SERVICE,
        GATT_PERM_READ
    },

    /* <<Characteristic>>, index 1 -- RX (Write / Write No Resp) */
    {
        ATTRIB_FLAG_VALUE_INCL,
        {
            LO_WORD(GATT_UUID_CHARACTERISTIC),
            HI_WORD(GATT_UUID_CHARACTERISTIC),
            (GATT_CHAR_PROP_WRITE | GATT_CHAR_PROP_WRITE_NO_RSP),
        },
        1,
        NULL,
        GATT_PERM_READ
    },

    /* RX characteristic value, index 2 */
    {
        ATTRIB_FLAG_VALUE_APPL | ATTRIB_FLAG_UUID_128BIT,
        {
            NUS_UUID_RX_CHAR_BYTES
        },
        0,
        NULL,
        GATT_PERM_WRITE
    },

    /* RX User Description, index 3 */
    {
        ATTRIB_FLAG_VOID | ATTRIB_FLAG_ASCII_Z,
        {
            LO_WORD(GATT_UUID_CHAR_USER_DESCR),
            HI_WORD(GATT_UUID_CHAR_USER_DESCR),
        },
        sizeof(nus_rx_user_desc) - 1,
        (void *)nus_rx_user_desc,
        GATT_PERM_READ
    },

    /* <<Characteristic>>, index 4 -- TX (Notify) */
    {
        ATTRIB_FLAG_VALUE_INCL,
        {
            LO_WORD(GATT_UUID_CHARACTERISTIC),
            HI_WORD(GATT_UUID_CHARACTERISTIC),
            GATT_CHAR_PROP_NOTIFY,
        },
        1,
        NULL,
        GATT_PERM_READ
    },

    /* TX characteristic value, index 5 */
    {
        ATTRIB_FLAG_VALUE_APPL | ATTRIB_FLAG_UUID_128BIT,
        {
            NUS_UUID_TX_CHAR_BYTES
        },
        0,
        NULL,
        GATT_PERM_NONE
    },

    /* CCCD, index 6 */
    {
        ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL,
        {
            LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
            HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
            LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT),
            HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT)
        },
        2,
        NULL,
        (GATT_PERM_READ | GATT_PERM_WRITE)
    },

    /* TX User Description, index 7 */
    {
        ATTRIB_FLAG_VOID | ATTRIB_FLAG_ASCII_Z,
        {
            LO_WORD(GATT_UUID_CHAR_USER_DESCR),
            HI_WORD(GATT_UUID_CHAR_USER_DESCR),
        },
        sizeof(nus_tx_user_desc) - 1,
        (void *)nus_tx_user_desc,
        GATT_PERM_READ
    },
};

/*============================================================================*
 *                              Functions
 *============================================================================*/

void nus_write_post_callback(uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id,
                             uint16_t attrib_index, uint16_t length, uint8_t *p_value)
{
    APP_PRINT_INFO5("nus_write_post_callback: conn_handle %d, cid %d, service_id %d, attrib_index 0x%x, length %d",
                    conn_handle, cid, service_id, attrib_index, length);
}

T_APP_RESULT nus_service_attr_read_cb(uint16_t conn_handle, uint16_t cid,
                                      T_SERVER_ID service_id, uint16_t attrib_index,
                                      uint16_t offset, uint16_t *p_length, uint8_t **pp_value)
{
    APP_PRINT_ERROR1("nus_service_attr_read_cb: attr not found, index %d", attrib_index);
    return APP_RESULT_ATTR_NOT_FOUND;
}

T_APP_RESULT nus_service_attr_write_cb(uint16_t conn_handle, uint16_t cid,
                                       T_SERVER_ID service_id, uint16_t attrib_index,
                                       T_WRITE_TYPE write_type, uint16_t length, uint8_t *p_value,
                                       P_FUN_EXT_WRITE_IND_POST_PROC *p_write_ind_post_proc)
{
    uint8_t conn_id = 0xFF;
    le_get_conn_id_by_handle(conn_handle, &conn_id);

    T_APP_RESULT cause = APP_RESULT_SUCCESS;

    APP_PRINT_INFO4("nus_service_attr_write_cb: attrib_index 0x%x, write_type 0x%x, conn_handle 0x%x, len %d",
                    attrib_index, write_type, conn_handle, length);

    *p_write_ind_post_proc = nus_write_post_callback;

    if (attrib_index == NUS_SVC_CHAR_RX_WRITE_INDEX)
    {
        if ((p_value == NULL) || (length == 0) || (length > NUS_RX_MAX_LEN))
        {
            APP_PRINT_ERROR1("nus_service_attr_write_cb: invalid rx length %d", length);
            cause = APP_RESULT_INVALID_VALUE_SIZE;
        }
        else
        {
            T_NUS_CALLBACK_DATA callback_data;
            callback_data.msg_type                  = SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE;
            callback_data.conn_id                   = conn_id;
            callback_data.conn_handle               = conn_handle;
            callback_data.cid                       = cid;
            callback_data.msg_data.write.opcode     = NUS_WRITE_RX;
            callback_data.msg_data.write.write_type = write_type;
            callback_data.msg_data.write.len        = length;
            callback_data.msg_data.write.p_value    = p_value;

            if (pfn_nus_service_cb)
            {
                pfn_nus_service_cb(service_id, (void *)&callback_data);
            }
        }
    }
    else
    {
        APP_PRINT_ERROR2("nus_service_attr_write_cb: attr not found, index 0x%x, length %d",
                         attrib_index, length);
        cause = APP_RESULT_ATTR_NOT_FOUND;
    }

    return cause;
}

void nus_service_cccd_update_cb(uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id,
                                uint16_t index, uint16_t cccbits)
{
    uint8_t conn_id = 0xFF;
    le_get_conn_id_by_handle(conn_handle, &conn_id);

    T_NUS_CALLBACK_DATA callback_data;
    bool is_handled = false;

    callback_data.conn_id     = conn_id;
    callback_data.conn_handle = conn_handle;
    callback_data.cid         = cid;
    callback_data.msg_type    = SERVICE_CALLBACK_TYPE_INDIFICATION_NOTIFICATION;

    APP_PRINT_INFO2("nus_service_cccd_update_cb: index = %d, cccbits 0x%x", index, cccbits);

    switch (index)
    {
    case NUS_SVC_CHAR_TX_CCCD_INDEX:
        callback_data.msg_data.notify_index =
            (cccbits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) ?
            NUS_NOTIFY_TX_ENABLE : NUS_NOTIFY_TX_DISABLE;
        is_handled = true;
        break;

    default:
        break;
    }

    if (pfn_nus_service_cb && is_handled)
    {
        pfn_nus_service_cb(service_id, (void *)&callback_data);
    }
}

const T_FUN_GATT_EXT_SERVICE_CBS nus_service_cbs =
{
    nus_service_attr_read_cb,
    nus_service_attr_write_cb,
    nus_service_cccd_update_cb
};

T_SERVER_ID nus_service_add_service(void *p_func, P_FUN_GATT_EXT_SEND_DATA_CB send_cb)
{
    if (false == gatt_svc_add(&nus_service_id,
                              (uint8_t *)nus_service_tbl,
                              sizeof(nus_service_tbl),
                              &nus_service_cbs, send_cb))
    {
        APP_PRINT_ERROR0("nus_service_add_service: fail");
        nus_service_id = 0xFF;
        return nus_service_id;
    }

    pfn_nus_service_cb = (P_FUN_EXT_SERVER_GENERAL_CB)p_func;
    return nus_service_id;
}

bool nus_service_send_data(uint16_t conn_handle, void *p_value, uint16_t length)
{
    if ((p_value == NULL) || (length == 0))
    {
        APP_PRINT_ERROR0("nus_service_send_data: invalid param");
        return false;
    }

    uint8_t  conn_id  = 0xFF;
    uint16_t mtu_size = 23;
    le_get_conn_id_by_handle(conn_handle, &conn_id);
    le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu_size, conn_id);
    if (length > mtu_size - 3)
    {
        APP_PRINT_ERROR2("nus_service_send_data: len %d > MTU-3 %d", length, mtu_size - 3);
        return false;
    }

    APP_PRINT_INFO1("nus_service_send_data: len %d", length);
    return gatt_svc_send_data(conn_handle, L2C_FIXED_CID_ATT, nus_service_id,
                              NUS_SVC_CHAR_TX_NOTIFY_INDEX,
                              p_value, length, GATT_PDU_TYPE_ANY);
}
