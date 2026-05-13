#include <string.h>
#include <trace.h>
#include <gap.h>
#include "gap_conn_le.h"
#include "bt_gatt_svc.h"
#include "hmi_private_service.h"

/*============================================================================*
 *                              Macros
 *============================================================================*/

typedef enum
{
    HMI_SVC_IDX_PRIMARY_SVC,           /* 0 */
    HMI_SVC_IDX_CMD_CHAR,              /* 1 */
    HMI_SVC_IDX_CMD_VAL,               /* 2 - HMI_SVC_CHAR_CMD_WRITE_INDEX */
    HMI_SVC_IDX_CMD_USER_DESC,         /* 3 */
    HMI_SVC_IDX_EVENT_CHAR,            /* 4 */
    HMI_SVC_IDX_EVENT_VAL,             /* 5 - HMI_SVC_CHAR_EVENT_NOTIFY_INDEX */
    HMI_SVC_IDX_EVENT_CCCD,            /* 6 - HMI_SVC_CHAR_EVENT_CCCD_INDEX */
    HMI_SVC_IDX_EVENT_USER_DESC,       /* 7 */
    HMI_SVC_IDX_STATUS_CHAR,           /* 8 */
    HMI_SVC_IDX_STATUS_VAL,            /* 9 - HMI_SVC_CHAR_STATUS_READ_INDEX */
    HMI_SVC_IDX_STATUS_USER_DESC,      /* 10 */
} T_HMI_SVC_IDX;

#define HMI_SVC_CHAR_CMD_WRITE_INDEX        HMI_SVC_IDX_CMD_VAL
#define HMI_SVC_CHAR_EVENT_NOTIFY_INDEX     HMI_SVC_IDX_EVENT_VAL
#define HMI_SVC_CHAR_EVENT_CCCD_INDEX       HMI_SVC_IDX_EVENT_CCCD
#define HMI_SVC_CHAR_STATUS_READ_INDEX      HMI_SVC_IDX_STATUS_VAL

/*============================================================================*
 *                              Local Variables
 *============================================================================*/

T_SERVER_ID hmi_service_id;

static uint8_t  hmi_status_value[HMI_STATUS_MAX_LEN];
static uint16_t hmi_status_len = 1;

static P_FUN_EXT_SERVER_GENERAL_CB pfn_hmi_service_cb = NULL;

static const uint8_t hmi_cmd_user_desc[]    = "HMI CMD";
static const uint8_t hmi_event_user_desc[]  = "HMI Event";
static const uint8_t hmi_status_user_desc[] = "HMI Status";

/* 128-bit Service UUID: 484D4953-0000-1000-8000-00805F9B34FB */
const uint8_t GATT_UUID128_HMI_SERVICE[16] =
{
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
    0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
    0x53, 0x49, 0x4D, 0x48
};

/*============================================================================*
 *                              GATT Service Table
 *============================================================================*/

const T_ATTRIB_APPL hmi_service_tbl[] =
{
    /* <<Primary Service>>, index 0 */
    {
        (ATTRIB_FLAG_VOID | ATTRIB_FLAG_LE),
        {
            LO_WORD(GATT_UUID_PRIMARY_SERVICE),
            HI_WORD(GATT_UUID_PRIMARY_SERVICE),
        },
        UUID_128BIT_SIZE,
        (void *)GATT_UUID128_HMI_SERVICE,
        GATT_PERM_READ
    },

    /* <<Characteristic>>, index 1 — CMD (Write) */
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

    /* CMD characteristic value, index 2 (HMI_SVC_CHAR_CMD_WRITE_INDEX) */
    {
        ATTRIB_FLAG_VALUE_APPL,
        {
            LO_WORD(BLE_UUID_HMI_CMD),
            HI_WORD(BLE_UUID_HMI_CMD),
        },
        0,
        NULL,
        GATT_PERM_WRITE
    },

    /* CMD User Description, index 3 */
    {
        ATTRIB_FLAG_VOID | ATTRIB_FLAG_ASCII_Z,
        {
            LO_WORD(GATT_UUID_CHAR_USER_DESCR),
            HI_WORD(GATT_UUID_CHAR_USER_DESCR),
        },
        sizeof(hmi_cmd_user_desc) - 1,
        (void *)hmi_cmd_user_desc,
        GATT_PERM_READ
    },

    /* <<Characteristic>>, index 4 — Event (Notify) */
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

    /* Event characteristic value, index 5 (HMI_SVC_CHAR_EVENT_NOTIFY_INDEX) */
    {
        ATTRIB_FLAG_VALUE_APPL,
        {
            LO_WORD(BLE_UUID_HMI_EVENT),
            HI_WORD(BLE_UUID_HMI_EVENT),
        },
        0,
        NULL,
        GATT_PERM_NONE
    },

    /* Client Characteristic Configuration, index 6 (HMI_SVC_CHAR_EVENT_CCCD_INDEX) */
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

    /* Event User Description, index 7 */
    {
        ATTRIB_FLAG_VOID | ATTRIB_FLAG_ASCII_Z,
        {
            LO_WORD(GATT_UUID_CHAR_USER_DESCR),
            HI_WORD(GATT_UUID_CHAR_USER_DESCR),
        },
        sizeof(hmi_event_user_desc) - 1,
        (void *)hmi_event_user_desc,
        GATT_PERM_READ
    },

    /* <<Characteristic>>, index 8 — Status (Read) */
    {
        ATTRIB_FLAG_VALUE_INCL,
        {
            LO_WORD(GATT_UUID_CHARACTERISTIC),
            HI_WORD(GATT_UUID_CHARACTERISTIC),
            GATT_CHAR_PROP_READ,
        },
        1,
        NULL,
        GATT_PERM_READ
    },

    /* Status characteristic value, index 9 (HMI_SVC_CHAR_STATUS_READ_INDEX) */
    {
        ATTRIB_FLAG_VALUE_APPL,
        {
            LO_WORD(BLE_UUID_HMI_STATUS),
            HI_WORD(BLE_UUID_HMI_STATUS),
        },
        0,
        NULL,
        GATT_PERM_READ
    },

    /* Status User Description, index 10 */
    {
        ATTRIB_FLAG_VOID | ATTRIB_FLAG_ASCII_Z,
        {
            LO_WORD(GATT_UUID_CHAR_USER_DESCR),
            HI_WORD(GATT_UUID_CHAR_USER_DESCR),
        },
        sizeof(hmi_status_user_desc) - 1,
        (void *)hmi_status_user_desc,
        GATT_PERM_READ
    },
};

/*============================================================================*
 *                              Functions
 *============================================================================*/

bool hmi_service_set_parameter(T_HMI_PARAM_TYPE param_type, uint16_t len, void *p_value)
{
    bool ret = true;

    switch (param_type)
    {
    case HMI_SERVICE_PARAM_STATUS:
        if (len <= HMI_STATUS_MAX_LEN)
        {
            memcpy(hmi_status_value, p_value, len);
            hmi_status_len = len;
        }
        else
        {
            ret = false;
        }
        break;
    default:
        ret = false;
        break;
    }

    if (!ret)
    {
        APP_PRINT_ERROR0("hmi_service_set_parameter failed");
    }
    return ret;
}

T_APP_RESULT hmi_service_attr_read_cb(uint16_t conn_handle, uint16_t cid,
                                      T_SERVER_ID service_id, uint16_t attrib_index,
                                      uint16_t offset, uint16_t *p_length, uint8_t **pp_value)
{
    T_APP_RESULT cause = APP_RESULT_SUCCESS;

    switch (attrib_index)
    {
    case HMI_SVC_CHAR_STATUS_READ_INDEX:
        {
            uint8_t conn_id = 0xFF;
            le_get_conn_id_by_handle(conn_handle, &conn_id);
            T_HMI_CALLBACK_DATA callback_data;
            callback_data.msg_type             = SERVICE_CALLBACK_TYPE_READ_CHAR_VALUE;
            callback_data.msg_data.read_value_index = HMI_READ_STATUS;
            callback_data.conn_id              = conn_id;
            callback_data.conn_handle          = conn_handle;
            callback_data.cid                  = cid;
            if (pfn_hmi_service_cb)
            {
                pfn_hmi_service_cb(service_id, (void *)&callback_data);
            }
            *pp_value = hmi_status_value;
            *p_length = hmi_status_len;
        }
        break;
    default:
        APP_PRINT_ERROR1("hmi_service_attr_read_cb: attr not found, index %d", attrib_index);
        cause = APP_RESULT_ATTR_NOT_FOUND;
        break;
    }

    return cause;
}

void hmi_write_post_callback(uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id,
                             uint16_t attrib_index, uint16_t length, uint8_t *p_value)
{
    APP_PRINT_INFO5("hmi_write_post_callback: conn_handle %d, cid %d, service_id %d, attrib_index 0x%x, length %d",
                    conn_handle, cid, service_id, attrib_index, length);
}

T_APP_RESULT hmi_service_attr_write_cb(uint16_t conn_handle, uint16_t cid,
                                       T_SERVER_ID service_id, uint16_t attrib_index,
                                       T_WRITE_TYPE write_type, uint16_t length, uint8_t *p_value,
                                       P_FUN_EXT_WRITE_IND_POST_PROC *p_write_ind_post_proc)
{
    uint8_t conn_id = 0xFF;
    le_get_conn_id_by_handle(conn_handle, &conn_id);

    T_APP_RESULT cause = APP_RESULT_SUCCESS;
    APP_PRINT_INFO3("hmi_service_attr_write_cb: write_type = 0x%x, conn_handle 0x%x, cid %d",
                    write_type, conn_handle, cid);
    *p_write_ind_post_proc = hmi_write_post_callback;

    if (attrib_index == HMI_SVC_CHAR_CMD_WRITE_INDEX)
    {
        if (p_value == NULL)
        {
            cause = APP_RESULT_INVALID_VALUE_SIZE;
        }
        else
        {
            T_HMI_CALLBACK_DATA callback_data;
            callback_data.msg_type               = SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE;
            callback_data.conn_id                = conn_id;
            callback_data.conn_handle            = conn_handle;
            callback_data.cid                    = cid;
            callback_data.msg_data.write.opcode     = HMI_WRITE_CMD;
            callback_data.msg_data.write.write_type = write_type;
            callback_data.msg_data.write.len        = length;
            callback_data.msg_data.write.p_value    = p_value;
            if (pfn_hmi_service_cb)
            {
                pfn_hmi_service_cb(service_id, (void *)&callback_data);
            }
        }
    }
    else
    {
        APP_PRINT_ERROR2("hmi_service_attr_write_cb: attr not found, index 0x%x, length %d",
                         attrib_index, length);
        cause = APP_RESULT_ATTR_NOT_FOUND;
    }

    return cause;
}

bool hmi_service_send_event(uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id,
                            void *p_value, uint16_t length)
{
    APP_PRINT_INFO0("hmi_service_send_event");
    return gatt_svc_send_data(conn_handle, cid, service_id,
                              HMI_SVC_CHAR_EVENT_NOTIFY_INDEX,
                              p_value, length, GATT_PDU_TYPE_ANY);
}

void hmi_service_cccd_update_cb(uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id,
                                uint16_t index, uint16_t cccbits)
{
    uint8_t conn_id = 0xFF;
    le_get_conn_id_by_handle(conn_handle, &conn_id);

    T_HMI_CALLBACK_DATA callback_data;
    bool is_handled = false;
    callback_data.conn_id     = conn_id;
    callback_data.conn_handle = conn_handle;
    callback_data.cid         = cid;
    callback_data.msg_type    = SERVICE_CALLBACK_TYPE_INDIFICATION_NOTIFICATION;
    APP_PRINT_INFO2("hmi_service_cccd_update_cb: index = %d, cccbits 0x%x", index, cccbits);

    switch (index)
    {
    case HMI_SVC_CHAR_EVENT_CCCD_INDEX:
        callback_data.msg_data.notify_index = (cccbits & GATT_CLIENT_CHAR_CONFIG_NOTIFY)
                                              ? HMI_NOTIFY_EVENT_ENABLE
                                              : HMI_NOTIFY_EVENT_DISABLE;
        is_handled = true;
        break;
    default:
        break;
    }

    if (pfn_hmi_service_cb && is_handled)
    {
        pfn_hmi_service_cb(service_id, (void *)&callback_data);
    }
}

const T_FUN_GATT_EXT_SERVICE_CBS hmi_service_cbs =
{
    hmi_service_attr_read_cb,
    hmi_service_attr_write_cb,
    hmi_service_cccd_update_cb
};

T_SERVER_ID hmi_service_add_service(void *p_func)
{
    if (false == gatt_svc_add(&hmi_service_id,
                              (uint8_t *)hmi_service_tbl,
                              sizeof(hmi_service_tbl),
                              &hmi_service_cbs, NULL))
    {
        APP_PRINT_ERROR0("hmi_service_add_service: fail");
        hmi_service_id = 0xFF;
        return hmi_service_id;
    }

    pfn_hmi_service_cb = (P_FUN_EXT_SERVER_GENERAL_CB)p_func;
    return hmi_service_id;
}
