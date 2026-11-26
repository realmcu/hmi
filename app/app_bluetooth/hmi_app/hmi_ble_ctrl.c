#include <string.h>
#include <stdlib.h>
#include <os_task.h>
#include <os_msg.h>
#include <os_sync.h>
#include <zephyr/kernel.h>
#include "hmi_ble_ctrl.h"
#include "trace.h"
#include "honey_protocol_log.h"
#include "hmi_ble_gap_msg.h"
#include "gap_msg.h"
#include "gap_le.h"
#include "gap_conn_le.h"
#include "hmi_ctrl_service.h"
#include "app_event.h"
#include "app_event_defs.h"

#define HMI_CONN_HANDLE_INVALID     0xFFFF

#define HMI_CTRL_TASK_STACK_SIZE    (4096)
#define HMI_CTRL_TASK_PRIORITY      (3)
#define HMI_CTRL_QUEUE_SIZE         (8)
#define HMI_CTRL_SEND_TIMEOUT_MS    (3000)
#define HMI_CTRL_MAX_IN_FLIGHT      (10u)

typedef struct
{
    uint16_t        conn_handle;
    const uint8_t  *p_data;
    uint16_t        len;
    void           *p_done_sem;
    int            *p_result;
} T_HMI_SEND_MSG;

typedef struct
{
    uint8_t  *p_data;   /* malloc'd by BT callback, freed by recv task */
    uint16_t  len;
} T_HMI_RECV_MSG;

static void *s_hmi_send_task_handle;
static void *s_hmi_send_queue_handle;
static void *s_hmi_recv_queue_handle;
static void *s_bt_done_sem;
static volatile uint16_t s_bt_credits;
static volatile uint8_t s_bt_in_flight;
static volatile bool s_bt_send_failed;
static uint8_t s_send_diag_count;

static volatile uint16_t hmi_conn_handle = HMI_CONN_HANDLE_INVALID;
static bool     hmi_event_cccd_enabled = false;

bool hmi_ble_ctrl_is_connected(void)
{
    return hmi_conn_handle != HMI_CONN_HANDLE_INVALID;
}

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
    s_bt_credits = result.credits;
    if (result.cause != GAP_SUCCESS)
    {
        s_bt_send_failed = true;
    }
    if (s_bt_in_flight > 0u)
    {
        s_bt_in_flight--;
    }
    os_sem_give(s_bt_done_sem);
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
            APP_PRINT_INFO2("app_hmi_callback: HMI CMD write, len %d, data %b",
                            p_hmi_cb->msg_data.write.len,
                            TRACE_BINARY(p_hmi_cb->msg_data.write.len, p_hmi_cb->msg_data.write.p_value));
            {
                T_HMI_RECV_MSG rx_msg;
                rx_msg.len    = p_hmi_cb->msg_data.write.len;
                rx_msg.p_data = malloc(rx_msg.len);
                if (rx_msg.p_data != NULL)
                {
                    memcpy(rx_msg.p_data, p_hmi_cb->msg_data.write.p_value, rx_msg.len);
                    if (!os_msg_send(s_hmi_recv_queue_handle, &rx_msg, 0))
                    {
                        APP_PRINT_ERROR0("app_hmi_callback: recv queue full, drop");
                        free(rx_msg.p_data);
                    }
                }
            }
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
                /* Broadcast to any interested subscriber (xfer receive session
                 * reset, UI, power module, ...). Delivery is asynchronous on
                 * app_task; the xfer reset used to be posted to l2_task via
                 * honey_protocol_run_on_dispatch — now it is just one subscriber. */
                {
                    app_evt_ble_disconnected_t ev =
                    {
                        .conn_id = gap_msg.msg_data.gap_conn_state_change.conn_id,
                        .reason  = gap_msg.msg_data.gap_conn_state_change.disc_cause,
                    };
                    (void)app_event_publish(EVT_BLE_DISCONNECTED, &ev, sizeof ev);
                }
                APP_PRINT_INFO0("gap_hmi_msg: disconnected");
                break;

            case GAP_CONN_STATE_CONNECTED:
                {
                    uint8_t conn_id = gap_msg.msg_data.gap_conn_state_change.conn_id;
                    hmi_conn_handle = le_get_conn_handle(conn_id);
                    APP_PRINT_INFO1("gap_hmi_msg: connected, conn_handle 0x%x", hmi_conn_handle);

                    app_evt_ble_connected_t ev =
                    {
                        .conn_id     = conn_id,
                        .conn_handle = hmi_conn_handle,
                    };
                    (void)app_event_publish(EVT_BLE_CONNECTED, &ev, sizeof ev);
                }
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

static void hmi_ble_send_task(void *p_param)
{
    T_HMI_SEND_MSG msg;

    os_msg_queue_create(&s_hmi_send_queue_handle, "hmisendQ",
                        HMI_CTRL_QUEUE_SIZE, sizeof(T_HMI_SEND_MSG));

    while (true)
    {
        if (os_msg_recv(s_hmi_send_queue_handle, &msg, 0xFFFFFFFF) != true)
        {
            continue;
        }

        APP_PRINT_INFO2("hmi_ble_send_task: conn_handle 0x%x, len %d",
                        msg.conn_handle, msg.len);

        uint8_t  conn_id = 0xFF;
        uint16_t mtu     = 23;
        le_get_conn_id_by_handle(msg.conn_handle, &conn_id);
        le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu, conn_id);
        uint16_t chunk = mtu - 3;
        uint16_t chunk_count = (msg.len + chunk - 1u) / chunk;
        uint32_t send_start = k_uptime_get_32();

        int      result = 0;
        uint16_t offset = 0;
        uint8_t credits = 0;
        uint32_t credit_wait_start = send_start;

        s_bt_in_flight = 0u;
        s_bt_send_failed = false;
        s_bt_credits = 0u;
        le_get_gap_param(GAP_PARAM_LE_REMAIN_CREDITS, &credits);

        while (offset < msg.len && result == 0)
        {
            if (credits == 0u || s_bt_in_flight >= HMI_CTRL_MAX_IN_FLIGHT)
            {
                if (s_bt_in_flight > 0u)
                {
                    if (!os_sem_take(s_bt_done_sem, HMI_CTRL_SEND_TIMEOUT_MS))
                    {
                        result = -2;
                        break;
                    }
                    credits = s_bt_credits;
                }
                else
                {
                    if ((uint32_t)(k_uptime_get_32() - credit_wait_start) >=
                        HMI_CTRL_SEND_TIMEOUT_MS)
                    {
                        result = -2;
                        break;
                    }
                    k_msleep(1);
                    le_get_gap_param(GAP_PARAM_LE_REMAIN_CREDITS, &credits);
                }
                if (s_bt_send_failed)
                {
                    result = -1;
                    break;
                }
                continue;
            }

            while (offset < msg.len && credits > 0u &&
                   s_bt_in_flight < HMI_CTRL_MAX_IN_FLIGHT)
            {
                uint16_t n = msg.len - offset;
                if (n > chunk)
                {
                    n = chunk;
                }

                s_bt_in_flight++;
                if (!hmi_ctrl_service_notify(msg.conn_handle,
                                             (void *)(msg.p_data + offset), n))
                {
                    s_bt_in_flight--;
                    APP_PRINT_ERROR0("hmi_ble_ctrl_task: notify failed");
                    result = -1;
                    break;
                }

                offset += n;
                credits--;
            }
        }

        /* The caller reuses the frame buffer only after every submitted
         * notification has completed. */
        while (s_bt_in_flight > 0u)
        {
            if (!os_sem_take(s_bt_done_sem, HMI_CTRL_SEND_TIMEOUT_MS))
            {
                result = -2;
                break;
            }
            if (s_bt_send_failed && result == 0)
            {
                result = -1;
            }
        }

        *msg.p_result = result;
        if (s_send_diag_count < 5u)
        {
            s_send_diag_count++;
            HONEY_PROTOCOL_LOG("HMI TX len=%u mtu=%u chunks=%u elapsed=%ums result=%d",
                               (unsigned)msg.len, (unsigned)mtu,
                               (unsigned)chunk_count,
                               (unsigned)(k_uptime_get_32() - send_start),
                               result);
        }
        os_sem_give(msg.p_done_sem);
    }
}

int hmi_ble_ctrl_receive(uint8_t *data, uint16_t max_len)
{
    T_HMI_RECV_MSG msg;

    if (os_msg_recv(s_hmi_recv_queue_handle, &msg, 0xFFFFFFFF) != true)
    {
        return -1;
    }

    APP_PRINT_INFO2("hmi_ble_ctrl_receive: len %d, data %b",
                    msg.len, TRACE_BINARY(msg.len, msg.p_data));

    uint16_t copy_len = (msg.len < max_len) ? msg.len : max_len;
    memcpy(data, msg.p_data, copy_len);
    free(msg.p_data);

    return (int)copy_len;
}

int hmi_ble_ctrl_send(const uint8_t *data, uint16_t len)
{
    void *done_sem = NULL;
    int   result   = 0;

    os_sem_create(&done_sem, "hmidone", 0, 1);

    T_HMI_SEND_MSG msg =
    {
        .conn_handle = hmi_conn_handle,
        .p_data      = data,
        .len         = len,
        .p_done_sem  = done_sem,
        .p_result    = &result,
    };

    os_msg_send(s_hmi_send_queue_handle, &msg, 0xFFFFFFFF);
    os_sem_take(done_sem, 0xFFFFFFFF);
    os_sem_delete(done_sem);

    return result;
}

void hmi_ble_ctrl_init(void)
{
    os_sem_create(&s_bt_done_sem, "hmibtdone", 0, HMI_CTRL_MAX_IN_FLIGHT);

    os_msg_queue_create(&s_hmi_recv_queue_handle, "hmirecvQ",
                        HMI_CTRL_QUEUE_SIZE, sizeof(T_HMI_RECV_MSG));

    hmi_ctrl_service_add_service(app_hmi_callback, app_hmi_send_data_cb);
    hmi_le_msg_cback_register(gap_hmi_msg);

    os_task_create(&s_hmi_send_task_handle, "hmisend", hmi_ble_send_task,
                   NULL, HMI_CTRL_TASK_STACK_SIZE, HMI_CTRL_TASK_PRIORITY);
}
