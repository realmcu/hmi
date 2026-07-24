#include <string.h>
#include <trace.h>
#include <gap.h>
#include "gap_conn_le.h"
#include "bt_gatt_svc.h"
#include "bt_types.h"
#include "hmi_stream_service.h"

/*============================================================================*
 *                              Attribute indices
 *============================================================================*/

typedef enum
{
    HMI_STREAM_IDX_PRIMARY_SVC,    /* 0 */
    HMI_STREAM_IDX_RX_CHAR,        /* 1 */
    HMI_STREAM_IDX_RX_VAL,         /* 2 - RX write  */
    HMI_STREAM_IDX_TX_CHAR,        /* 3 */
    HMI_STREAM_IDX_TX_VAL,         /* 4 - TX notify */
    HMI_STREAM_IDX_TX_CCCD,        /* 5 */
} T_HMI_STREAM_SVC_IDX;

#define HMI_STREAM_RX_WRITE_INDEX       HMI_STREAM_IDX_RX_VAL
#define HMI_STREAM_TX_NOTIFY_INDEX      HMI_STREAM_IDX_TX_VAL
#define HMI_STREAM_TX_CCCD_INDEX        HMI_STREAM_IDX_TX_CCCD

/*============================================================================*
 *                              Local Variables
 *============================================================================*/

T_SERVER_ID hmi_stream_service_id;

static hmi_stream_rx_cb_t   s_rx_cb   = NULL;
static hmi_stream_cccd_cb_t s_cccd_cb = NULL;

/* 128-bit Service UUID: 484D5354-0000-1000-8000-00805F9B34FB */
const uint8_t GATT_UUID128_HMI_STREAM_SERVICE[16] =
{
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
    0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
    0x54, 0x53, 0x4D, 0x48
};

/*============================================================================*
 *                              GATT Service Table
 *============================================================================*/

static const T_ATTRIB_APPL hmi_stream_service_tbl[] =
{
    /* <<Primary Service>>, index 0 */
    {
        (ATTRIB_FLAG_VOID | ATTRIB_FLAG_LE),
        {
            LO_WORD(GATT_UUID_PRIMARY_SERVICE),
            HI_WORD(GATT_UUID_PRIMARY_SERVICE),
        },
        UUID_128BIT_SIZE,
        (void *)GATT_UUID128_HMI_STREAM_SERVICE,
        GATT_PERM_READ
    },

    /* <<Characteristic>>, index 1 — Stream RX (Write / Write No Response) */
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

    /* Stream RX value, index 2 (HMI_STREAM_RX_WRITE_INDEX) */
    {
        ATTRIB_FLAG_VALUE_APPL,
        {
            LO_WORD(BLE_UUID_HMI_STREAM_RX),
            HI_WORD(BLE_UUID_HMI_STREAM_RX),
        },
        0,
        NULL,
        GATT_PERM_WRITE
    },

    /* <<Characteristic>>, index 3 — Stream TX (Notify) */
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

    /* Stream TX value, index 4 (HMI_STREAM_TX_NOTIFY_INDEX) */
    {
        ATTRIB_FLAG_VALUE_APPL,
        {
            LO_WORD(BLE_UUID_HMI_STREAM_TX),
            HI_WORD(BLE_UUID_HMI_STREAM_TX),
        },
        0,
        NULL,
        GATT_PERM_NONE
    },

    /* Client Characteristic Configuration, index 5 (HMI_STREAM_TX_CCCD_INDEX) */
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
};

/*============================================================================*
 *                              Callbacks
 *============================================================================*/

static T_APP_RESULT hmi_stream_attr_read_cb(uint16_t conn_handle, uint16_t cid,
                                            T_SERVER_ID service_id, uint16_t attrib_index,
                                            uint16_t offset, uint16_t *p_length, uint8_t **pp_value)
{
    (void)conn_handle; (void)cid; (void)service_id;
    (void)attrib_index; (void)offset; (void)p_length; (void)pp_value;
    /* No readable application value in the stream service. */
    return APP_RESULT_ATTR_NOT_FOUND;
}

static T_APP_RESULT hmi_stream_attr_write_cb(uint16_t conn_handle, uint16_t cid,
                                             T_SERVER_ID service_id, uint16_t attrib_index,
                                             T_WRITE_TYPE write_type, uint16_t length, uint8_t *p_value,
                                             P_FUN_EXT_WRITE_IND_POST_PROC *p_write_ind_post_proc)
{
    (void)cid; (void)service_id; (void)write_type;
    *p_write_ind_post_proc = NULL;

    if (attrib_index == HMI_STREAM_RX_WRITE_INDEX)
    {
        if (p_value == NULL || length == 0)
        {
            return APP_RESULT_INVALID_VALUE_SIZE;
        }
        if (s_rx_cb)
        {
            s_rx_cb(conn_handle, p_value, length);
        }
        return APP_RESULT_SUCCESS;
    }

    APP_PRINT_ERROR1("hmi_stream_attr_write_cb: attr not found, index 0x%x", attrib_index);
    return APP_RESULT_ATTR_NOT_FOUND;
}

static void hmi_stream_cccd_update_cb(uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id,
                                      uint16_t index, uint16_t cccbits)
{
    (void)cid; (void)service_id;
    if (index == HMI_STREAM_TX_CCCD_INDEX && s_cccd_cb)
    {
        s_cccd_cb(conn_handle, (cccbits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) != 0);
    }
}

/* GATT notify-complete callback; stream TX is fire-and-forget so we only log. */
static void hmi_stream_send_data_cb(T_EXT_SEND_DATA_RESULT result)
{
    if (result.cause != GAP_SUCCESS)
    {
        APP_PRINT_ERROR2("hmi_stream_send_data_cb: notify fail, conn 0x%x, cause 0x%x",
                         result.conn_handle, result.cause);
    }
}

static const T_FUN_GATT_EXT_SERVICE_CBS hmi_stream_service_cbs =
{
    hmi_stream_attr_read_cb,
    hmi_stream_attr_write_cb,
    hmi_stream_cccd_update_cb
};

/*============================================================================*
 *                              Public API
 *============================================================================*/

bool hmi_stream_service_notify(uint16_t conn_handle, const void *p_value, uint16_t length)
{
    if (p_value == NULL || length == 0)
    {
        return false;
    }
    return gatt_svc_send_data(conn_handle, L2C_FIXED_CID_ATT, hmi_stream_service_id,
                              HMI_STREAM_TX_NOTIFY_INDEX,
                              (void *)p_value, length, GATT_PDU_TYPE_ANY);
}

T_SERVER_ID hmi_stream_service_add_service(hmi_stream_rx_cb_t rx_cb,
                                           hmi_stream_cccd_cb_t cccd_cb)
{
    s_rx_cb   = rx_cb;
    s_cccd_cb = cccd_cb;

    if (false == gatt_svc_add(&hmi_stream_service_id,
                              (uint8_t *)hmi_stream_service_tbl,
                              sizeof(hmi_stream_service_tbl),
                              &hmi_stream_service_cbs, hmi_stream_send_data_cb))
    {
        APP_PRINT_ERROR0("hmi_stream_service_add_service: fail");
        hmi_stream_service_id = 0xFF;
    }
    return hmi_stream_service_id;
}
