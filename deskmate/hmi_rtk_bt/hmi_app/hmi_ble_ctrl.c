#include <string.h>
#if defined(__ZEPHYR__) && defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif
#include "hmi_ble_ctrl.h"
#include "trace.h"
#include "hmi_ble_gap_msg.h"
#include "gap_msg.h"
#include "gap_conn_le.h"
#include "hmi_ctrl_service.h"

#define HMI_CONN_HANDLE_INVALID  0xFFFF

static uint16_t hmi_conn_handle    = HMI_CONN_HANDLE_INVALID;
static bool     hmi_event_cccd_enabled = false;

static void app_hmi_send_data_cb(T_EXT_SEND_DATA_RESULT result)
{
    if (result.cause == GAP_SUCCESS)
    {
        APP_PRINT_INFO2("app_hmi_send_data_cb: notify sent ok, conn_handle 0x%x, credits %d",
                        result.conn_handle, result.credits);
    }
    else
    {
        APP_PRINT_ERROR2("app_hmi_send_data_cb: notify sent fail, conn_handle 0x%x, cause 0x%x",
                         result.conn_handle, result.cause);
    }
    printf("app_hmi_send_data_cb: notify sent %s, conn_handle 0x%x, credits %d\r\n",
           (result.cause == GAP_SUCCESS) ? "ok" : "fail",
           result.conn_handle, result.credits);
}

static T_APP_RESULT app_hmi_callback(T_SERVER_ID service_id, void *p_data)
{
    T_APP_RESULT app_result = APP_RESULT_SUCCESS;

    if (service_id == hmi_ctrl_service_id)
    {
        T_HMI_CALLBACK_DATA *p_hmi_cb = (T_HMI_CALLBACK_DATA *)p_data;
        switch (p_hmi_cb->msg_type)
        {
        case SERVICE_CALLBACK_TYPE_INDIFICATION_NOTIFICATION:
            if (p_hmi_cb->msg_data.notify_index == HMI_NOTIFY_EVENT_ENABLE)
            {
                hmi_event_cccd_enabled = true;
                APP_PRINT_INFO0("app_hmi_callback: HMI_NOTIFY_EVENT_ENABLE");
            }
            else
            {
                hmi_event_cccd_enabled = false;
                APP_PRINT_INFO0("app_hmi_callback: HMI_NOTIFY_EVENT_DISABLE");
            }
            break;

        case SERVICE_CALLBACK_TYPE_READ_CHAR_VALUE:
            APP_PRINT_INFO0("app_hmi_callback: HMI STATUS read");
            break;

        case SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE:
            APP_PRINT_INFO3("app_hmi_callback: HMI CMD write, type %d, len %d, data %b",
                            p_hmi_cb->msg_data.write.write_type,
                            p_hmi_cb->msg_data.write.len,
                            TRACE_BINARY(p_hmi_cb->msg_data.write.len, p_hmi_cb->msg_data.write.p_value));
            break;

        default:
            break;
        }
    }

    return app_result;
}

static void gap_hmi_msg(T_IO_MSG *p_gap_msg)
{
    APP_PRINT_TRACE2("gap_hmi_msg: type %d, subtype %d", p_gap_msg->type, p_gap_msg->subtype);
    T_LE_GAP_MSG gap_msg;
    memcpy(&gap_msg, &p_gap_msg->u.param, sizeof(p_gap_msg->u.param));

    switch (p_gap_msg->type)
    {
    case IO_MSG_TYPE_BT_STATUS:
        switch (p_gap_msg->subtype)
        {
        case GAP_MSG_LE_CONN_STATE_CHANGE:
            switch ((T_GAP_CONN_STATE)gap_msg.msg_data.gap_conn_state_change.new_state)
            {
            case GAP_CONN_STATE_DISCONNECTED:
                hmi_conn_handle    = HMI_CONN_HANDLE_INVALID;
                hmi_event_cccd_enabled = false;
                APP_PRINT_INFO0("gap_hmi_msg: disconnected");
                break;

            case GAP_CONN_STATE_CONNECTED:
                hmi_conn_handle = le_get_conn_handle(
                                      gap_msg.msg_data.gap_conn_state_change.conn_id);
                APP_PRINT_INFO1("gap_hmi_msg: connected, conn_handle 0x%x", hmi_conn_handle);
                break;

            default:
                break;
            }
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

void hmi_ble_ctrl_init(void)
{
    hmi_ctrl_service_add_service(app_hmi_callback, app_hmi_send_data_cb);
    hmi_le_msg_cback_register(gap_hmi_msg);
}

#if defined(__ZEPHYR__) && defined(CONFIG_SHELL)
static int cmd_hmi_notify(const struct shell *sh, size_t argc, char **argv)
{
    if (hmi_conn_handle == HMI_CONN_HANDLE_INVALID)
    {
        shell_error(sh, "not connected");
        return -ENODEV;
    }
    if (!hmi_event_cccd_enabled)
    {
        shell_error(sh, "notification not enabled by peer");
        return -EAGAIN;
    }

    uint8_t *data = (uint8_t *)argv[1];
    uint16_t len  = (uint16_t)strlen(argv[1]);

    bool ok = hmi_ctrl_service_send_data(hmi_conn_handle, data, len);
    if (!ok)
    {
        shell_error(sh, "send failed (no credits or disconnected)");
        return -EIO;
    }

    shell_print(sh, "queued %u bytes", len);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(hmi_sub,
                               SHELL_CMD_ARG(notify, NULL, "Send BLE notification <text>", cmd_hmi_notify, 2, 0),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(hmi, &hmi_sub, "HMI BLE commands", NULL);
#endif /* CONFIG_SHELL */
