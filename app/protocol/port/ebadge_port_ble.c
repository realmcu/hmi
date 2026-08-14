/**
 * @file    ebadge_port_ble.c
 * @brief   Bind app/protocol/ to the hmi_ctrl_service GATT layer on this SoC.
 *
 * Compared to the old hmi_ble_ctrl.c this shim is thin -- no send queue, no
 * send-task, no rx queue.  Rationale:
 *   - the old proto layer needed a blocking proto_send with an ACK sem, so
 *     Notify had to be serialised through a task with per-frame wait;
 *   - the V1.2 protocol has no ACK; hmi_ctrl_service_notify() returns as soon
 *     as the ATT layer queues the packet, so we can call it inline on
 *     l2_task without any extra serialisation.
 *
 * Threading:
 *   - app_hmi_callback fires on BT stack context; we memcpy + enqueue via
 *     ebadge_task_on_rx() and return immediately.
 *   - gap_hmi_msg fires on the GAP dispatcher (also BT stack side); we only
 *     mutate two module-level statics (conn_handle, cccd_enabled).  Reads on
 *     l2_task are of primitive types; no lock needed given how often these
 *     transition -- and the worst case is one dropped notify.
 */
#include <string.h>
#include <stdbool.h>

#include <app_msg.h>
#include <gap_conn_le.h>

#include "hmi_ctrl_service.h"
#include "hmi_ble_gap_msg.h"          /* hmi_le_msg_cback_register */

#include "ebadge_port_ble.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"
#include "../wifi_xfer/xfer_session.h"

#define HMI_CONN_HANDLE_INVALID     0xFFFF

/* -------- module-owned state -------- */
static uint16_t s_conn_handle    = HMI_CONN_HANDLE_INVALID;
static bool     s_cccd_enabled   = false;
static bool     s_inited         = false;

/*----------------------------------------------------------------------------*
 *  GATT ext-service tx-complete callback  (no per-notify wait -- log only)
 *
 *  Exposed non-static so hmi_ble_profile_init() can hand this and the
 *  service callback below to hmi_ctrl_service_add_service() at the right
 *  time in the BT stack init sequence -- i.e. right after gatt_svc_init(),
 *  alongside NUS and OTA.  See ebadge_port_ble_bind_service() below.
 *----------------------------------------------------------------------------*/
void ebadge_port_ble__on_send_data_cb(T_EXT_SEND_DATA_RESULT result)
{
    if (result.cause != GAP_SUCCESS)
    {
        EBADGE_ERR2("notify tx-fail conn=0x%x cause=0x%x",
                    result.conn_handle, result.cause);
    }
    /* success path is silent to keep the trace clean */
}

/*----------------------------------------------------------------------------*
 *  Service callback  -- CCCD updates and inbound writes on the CMD char
 *  (non-static; see comment on on_send_data_cb above)
 *----------------------------------------------------------------------------*/
T_APP_RESULT ebadge_port_ble__on_hmi_service_cb(T_SERVER_ID service_id, void *p_data)
{
    if (service_id != hmi_ctrl_service_id)
    {
        return APP_RESULT_SUCCESS;
    }
    T_HMI_CALLBACK_DATA *cb = (T_HMI_CALLBACK_DATA *)p_data;
    switch (cb->msg_type)
    {
    case SERVICE_CALLBACK_TYPE_INDIFICATION_NOTIFICATION:
        s_cccd_enabled = (cb->msg_data.notify_index == HMI_NOTIFY_EVENT_ENABLE);
        EBADGE_LOG1("cccd=%d", (int)s_cccd_enabled);
        break;

    case SERVICE_CALLBACK_TYPE_WRITE_CHAR_VALUE:
        /* Push raw bytes into l2_task -- reassembler runs there. */
        (void)ebadge_task_on_rx(cb->msg_data.write.p_value,
                                cb->msg_data.write.len);
        break;

    case SERVICE_CALLBACK_TYPE_READ_CHAR_VALUE:
        /* STATUS read: nothing here yet -- ctrl_service returns whatever
         * was staged via hmi_ctrl_service_set_parameter().  If we ever
         * want to expose runtime status through the STATUS char, populate
         * it from here.  For now the RX/TX chars carry everything. */
        break;

    default:
        break;
    }
    return APP_RESULT_SUCCESS;
}

/*----------------------------------------------------------------------------*
 *  GAP msg -- track connection state
 *----------------------------------------------------------------------------*/
static void on_gap_msg(T_IO_MSG *msg)
{
    T_LE_GAP_MSG gap;
    memcpy(&gap, &msg->u.param, sizeof(msg->u.param));

    if (msg->type != IO_MSG_TYPE_BT_STATUS ||
        msg->subtype != GAP_MSG_LE_CONN_STATE_CHANGE)
    {
        return;
    }
    T_GAP_CONN_STATE ns =
        (T_GAP_CONN_STATE)gap.msg_data.gap_conn_state_change.new_state;
    switch (ns)
    {
    case GAP_CONN_STATE_CONNECTED:
        s_conn_handle = le_get_conn_handle(
                            gap.msg_data.gap_conn_state_change.conn_id);
        EBADGE_LOG1("connected conn_handle=0x%x", s_conn_handle);
        break;

    case GAP_CONN_STATE_DISCONNECTED:
        s_conn_handle  = HMI_CONN_HANDLE_INVALID;
        s_cccd_enabled = false;
        /* Tear down any in-flight xfer on the same context as command
         * handlers (l2_task) so we do not race an inbound EBXF chunk mid-
         * abort.  post_call is safe from this GAP-dispatcher context. */
        (void)ebadge_task_post_call((ebadge_post_fn_t)xfer_session_abort, NULL);
        /* Same reason for the reassembler: a 0x02 SEND_FILE that announced a
         * body and then dropped would otherwise leave the RX stream in raw
         * mode, swallowing the next connection's frames as file bytes.     */
        (void)ebadge_task_post_call((ebadge_post_fn_t)ebadge_task_rx_reset, NULL);
        EBADGE_LOG("disconnected");
        break;

    default:
        break;
    }
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void ebadge_port_ble_init(void)
{
    if (s_inited) { return; }
    /* NOTE: hmi_ctrl_service_add_service() is NOT called here.  Ext-server
     * services must be registered *after* gatt_svc_init() runs (that happens
     * inside hmi_ble_profile_init(), driven by bt_task).  ebadge_task_init()
     * runs from main() before bt_task drains its init queue, so adding here
     * silently drops the service.  hmi_ble_profile_init() instead calls
     * hmi_ctrl_service_add_service(ebadge_port_ble__on_hmi_service_cb,
     *                              ebadge_port_ble__on_send_data_cb)
     * right next to hmi_ble_nus_init() / app_ota_service_init().           */
    hmi_le_msg_cback_register(on_gap_msg);
    s_inited = true;
    EBADGE_LOG("port_ble: GAP msg hook registered "
               "(service registration is done in hmi_ble_profile_init)");
}

bool ebadge_port_ble_is_connected(void)
{
    return s_conn_handle != HMI_CONN_HANDLE_INVALID;
}

bool ebadge_port_ble_cccd_enabled(void)
{
    return s_cccd_enabled;
}

int ebadge_port_ble_notify(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        return -1;
    }
    if (s_conn_handle == HMI_CONN_HANDLE_INVALID)
    {
        return -2;
    }
    /* hmi_ctrl_service_notify uses profile_server_ext / write-response path;
     * it queues internally, no need to wait for the send-data cb here. */
    return hmi_ctrl_service_notify(s_conn_handle, (void *)data, len) ? 0 : -3;
}
