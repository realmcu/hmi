
/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <trace.h>
#include <string.h>
#include <stdlib.h>
#include <os_timer.h>
#include <gap.h>
#include <gap_adv.h>
#include <gap_bond_le.h>
#include <gap_conn_le.h>
#include "hmi_bt_task.h"
#include "hmi_ble_gap_init.h"
#include "hmi_ble_gap_msg.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_ota_service.h"

static T_GAP_DEV_STATE gap_dev_state = {0, 0, 0, 0};                 /**< GAP device state */
static T_GAP_CONN_STATE gap_conn_state = GAP_CONN_STATE_DISCONNECTED; /**< GAP connection state */
static app_evt_ble_conn_param_t hmi_conn_info = {0, 0, 0, 0};       /**< Cached BLE connection info */

static T_LE_MSG_CBACK_ITEM gap_msg_list = {NULL, NULL};

static void update_conn_info(uint8_t conn_id);

/* le_adv_start() may be rejected (cause 0x2 GAP_CAUSE_INVALID_STATE) while a
 * link is still tearing down: right after a central/master disconnect the
 * controller has not finished releasing the link.  (A slave/peripheral
 * disconnect never hits this, which is why the old direct le_adv_start()
 * worked there.)  This transient is NOT reflected in T_GAP_DEV_STATE, so a
 * dev-state-change retry never fires -- retry on a short one-shot timer. */
static void    *s_adv_retry_timer     = NULL;
static uint8_t  s_adv_retry_cnt       = 0;

#define ADV_RETRY_TIMER_ID   1u
#define ADV_RETRY_MS         200u
#define ADV_RETRY_MAX        10u    /* ~2s worst case */

static void adv_retry_timer_cb(void *p_handle)
{
    (void)p_handle;
    /* Re-attempt from the timer task; le_adv_start() only posts to the stack. */
    hmi_ble_gap_start_adv();
}

void hmi_ble_gap_start_adv(void)
{
    uint8_t adv_st = gap_dev_state.gap_adv_state;
    if (adv_st == GAP_ADV_STATE_ADVERTISING || adv_st == GAP_ADV_STATE_START)
    {
        /* Already advertising / starting (e.g. a master connection did not stop
         * our advertising) -- nothing to do. */
        s_adv_retry_cnt = 0;
        if (s_adv_retry_timer != NULL)
        {
            os_timer_stop(&s_adv_retry_timer);
        }
        return;
    }

    T_GAP_CAUSE cause = le_adv_start();
    if (cause == GAP_CAUSE_SUCCESS)
    {
        s_adv_retry_cnt = 0;
        if (s_adv_retry_timer != NULL)
        {
            os_timer_stop(&s_adv_retry_timer);
        }
        return;
    }

    /* Typically cause 0x2 (GAP_CAUSE_INVALID_STATE) right after a central/master
     * disconnect: the controller has not finished tearing the link down yet.
     * This transient is NOT reflected in T_GAP_DEV_STATE, so a dev-state retry
     * never triggers -- retry on a short one-shot timer instead. */
    if (s_adv_retry_cnt < ADV_RETRY_MAX)
    {
        s_adv_retry_cnt++;
        if (s_adv_retry_timer == NULL)
        {
            os_timer_create(&s_adv_retry_timer, "adv_retry", ADV_RETRY_TIMER_ID,
                            ADV_RETRY_MS, false, adv_retry_timer_cb);
        }
        os_timer_restart(&s_adv_retry_timer, ADV_RETRY_MS);
        APP_PRINT_WARN2("hmi_ble_gap_start_adv: le_adv_start cause 0x%x, retry #%d scheduled",
                        cause, s_adv_retry_cnt);
    }
    else
    {
        s_adv_retry_cnt = 0;
        APP_PRINT_ERROR0("hmi_ble_gap_start_adv: gave up restarting advertising");
    }
}

uint8_t hmi_ble_gap_get_adv_state(void)
{
    return gap_dev_state.gap_adv_state;
}


void hmi_le_msg_cback_register(P_LE_MSG_HANDLER_CBACK
                               cback)//todo for return bool, cpp check fail
{
    T_LE_MSG_CBACK_ITEM *p_item = malloc(sizeof(T_LE_MSG_CBACK_ITEM));
    if (p_item == NULL)
    {
        return;
    }

    p_item->cback = cback;

    ble_slist_append(&(gap_msg_list.slist), &(p_item->slist));

}

void hmi_le_msg_cback_unregister(P_LE_MSG_HANDLER_CBACK cback)
{
    ble_slist_t *node;
    for (node = ble_slist_first(&(gap_msg_list.slist)); node; node = ble_slist_next(node))
    {
        T_LE_MSG_CBACK_ITEM *p_item = ble_container_of(node, T_LE_MSG_CBACK_ITEM, slist);
        if (p_item->cback == cback)
        {
            ble_slist_remove(&(gap_msg_list.slist), &(p_item->slist));
            free(p_item);
            break;
        }
    }
}


static void app_handle_dev_state_evt(T_GAP_DEV_STATE new_state, uint16_t cause)
{
    APP_PRINT_INFO3("app_handle_dev_state_evt: init state %d, adv state %d, cause 0x%x",
                    new_state.gap_init_state, new_state.gap_adv_state, cause);
    if (gap_dev_state.gap_init_state != new_state.gap_init_state)
    {
        if (new_state.gap_init_state == GAP_INIT_STATE_STACK_READY)
        {
            APP_PRINT_INFO0("GAP stack ready");
            /*stack ready*/
            hmi_ble_gap_start_adv();
        }
    }

    if (gap_dev_state.gap_adv_state != new_state.gap_adv_state)
    {
        if (new_state.gap_adv_state == GAP_ADV_STATE_IDLE)
        {
            if (new_state.gap_adv_sub_state == GAP_ADV_TO_IDLE_CAUSE_CONN)
            {
                APP_PRINT_INFO0("GAP adv stoped: because connection created");
            }
            else
            {
                APP_PRINT_INFO0("GAP adv stoped");
            }
        }
        else if (new_state.gap_adv_state == GAP_ADV_STATE_ADVERTISING)
        {
            APP_PRINT_INFO0("GAP adv start");
        }
    }

    if (gap_dev_state.gap_scan_state != new_state.gap_scan_state)
    {
        if (new_state.gap_scan_state == GAP_SCAN_STATE_SCANNING)
        {
            APP_PRINT_INFO0("GAP scan start");
        }
        else if (new_state.gap_scan_state == GAP_SCAN_STATE_IDLE)
        {
            APP_PRINT_INFO0("GAP scan stop");
        }
    }

    /* Central (file-send) role removed; adv state changes no longer need to
     * drive an adv-stop -> scan-start handoff. */

    gap_dev_state = new_state;
}

/**
 * @brief    Handle msg GAP_MSG_LE_CONN_STATE_CHANGE
 * @note     All the gap conn state events are pre-handled in this function.
 *           Then the event handling function shall be called according to the new_state
 * @param[in] conn_id Connection ID
 * @param[in] new_state  New gap connection state
 * @param[in] disc_cause Use this cause when new_state is GAP_CONN_STATE_DISCONNECTED
 * @return   void
 */
static void app_handle_conn_state_evt(uint8_t conn_id, T_GAP_CONN_STATE new_state,
                                      uint16_t disc_cause)
{
    APP_PRINT_INFO4("app_handle_conn_state_evt: conn_id %d old_state %d new_state %d, disc_cause 0x%x",
                    conn_id, gap_conn_state, new_state, disc_cause);
    switch (new_state)
    {
    case GAP_CONN_STATE_DISCONNECTED:
        {
            if ((disc_cause != (HCI_ERR | HCI_ERR_REMOTE_USER_TERMINATE))
                && (disc_cause != (HCI_ERR | HCI_ERR_LOCAL_HOST_TERMINATE)))
            {
                APP_PRINT_ERROR1("app_handle_conn_state_evt: connection lost cause 0x%x", disc_cause);
            }
            /* Central (file-send) role removed; only server-side OTA glue
             * still cares about disconnection. */
            app_ota_glue_link_disconnected(conn_id, disc_cause);
            memset(&hmi_conn_info, 0, sizeof(hmi_conn_info));
            hmi_ble_gap_start_adv();    /* return to receiver(advertising) state */
        }
        break;

    case GAP_CONN_STATE_CONNECTED:
        {
            uint16_t conn_interval;
            uint16_t conn_latency;
            uint16_t conn_supervision_timeout;
            uint8_t  remote_bd[6];
            T_GAP_REMOTE_ADDR_TYPE remote_bd_type;

            le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &conn_interval, conn_id);
            le_get_conn_param(GAP_PARAM_CONN_LATENCY, &conn_latency, conn_id);
            le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &conn_supervision_timeout, conn_id);
            le_get_conn_addr(conn_id, remote_bd, &remote_bd_type);
            APP_PRINT_INFO5("GAP_CONN_STATE_CONNECTED:remote_bd %s, remote_addr_type %d, conn_interval 0x%x, conn_latency 0x%x, conn_supervision_timeout 0x%x",
                            TRACE_BDADDR(remote_bd), remote_bd_type,
                            conn_interval, conn_latency, conn_supervision_timeout);

            /* Central (file-send) role removed; we only act as GATT server. */
            update_conn_info(conn_id);
            (void)app_event_publish(EVT_BLE_CONN_PARAM, &hmi_conn_info, sizeof hmi_conn_info);
            app_ota_glue_link_connected(conn_id, 0, remote_bd);

            /* update connection interval to 30ms */
            uint16_t interval_min = 24;   /* 24 * 1.25ms = 30ms */
            uint16_t interval_max = 24;   /* 24 * 1.25ms = 30ms */
            uint16_t latency = 0;
            uint16_t supervision_timeout = 500; /* 500 * 10ms = 5000ms */
            uint16_t min_ce_len = 2 * (interval_min - 1);
            uint16_t max_ce_len = 2 * (interval_max - 1);
            le_update_conn_param(conn_id, interval_min, interval_max, latency,
                                 supervision_timeout, min_ce_len, max_ce_len);
        }
        break;

    default:
        break;
    }
    gap_conn_state = new_state;
}

/**
 * @brief    Handle msg GAP_MSG_LE_AUTHEN_STATE_CHANGE
 * @note     All the gap authentication state events are pre-handled in this function.
 *           Then the event handling function shall be called according to the new_state
 * @param[in] conn_id Connection ID
 * @param[in] new_state  New authentication state
 * @param[in] cause Use this cause when new_state is GAP_AUTHEN_STATE_COMPLETE
 * @return   void
 */
static void app_handle_authen_state_evt(uint8_t conn_id, uint8_t new_state, uint16_t cause)
{
    APP_PRINT_INFO2("app_handle_authen_state_evt:conn_id %d, cause 0x%x", conn_id, cause);

    switch (new_state)
    {
    case GAP_AUTHEN_STATE_STARTED:
        {
            APP_PRINT_INFO0("app_handle_authen_state_evt: GAP_AUTHEN_STATE_STARTED");
        }
        break;

    case GAP_AUTHEN_STATE_COMPLETE:
        {
            if (cause == GAP_SUCCESS)
            {
                // gatts_start_discovery(conn_id);

                APP_PRINT_INFO0("app_handle_authen_state_evt: GAP_AUTHEN_STATE_COMPLETE pair success");

            }
            else
            {
                APP_PRINT_INFO0("app_handle_authen_state_evt: GAP_AUTHEN_STATE_COMPLETE pair failed");
            }
        }
        break;

    default:
        {
            APP_PRINT_ERROR1("app_handle_authen_state_evt: unknown newstate %d", new_state);
        }
        break;
    }
}

/**
 * @brief    Handle msg GAP_MSG_LE_CONN_MTU_INFO
 * @note     This msg is used to inform APP that exchange mtu procedure is completed.
 * @param[in] conn_id Connection ID
 * @param[in] mtu_size  New mtu size
 * @return   void
 */
static void app_handle_conn_mtu_info_evt(uint8_t conn_id, uint16_t mtu_size)
{
    APP_PRINT_INFO2("app_handle_conn_mtu_info_evt: conn_id %d, mtu_size %d", conn_id, mtu_size);
}

/**
 * @brief    Handle msg GAP_MSG_LE_CONN_PARAM_UPDATE
 * @note     All the connection parameter update change  events are pre-handled in this function.
 * @param[in] conn_id Connection ID
 * @param[in] status  New update state
 * @param[in] cause Use this cause when status is GAP_CONN_PARAM_UPDATE_STATUS_FAIL
 * @return   void
 */
static void app_handle_conn_param_update_evt(uint8_t conn_id, uint8_t status, uint16_t cause)
{
    switch (status)
    {
    case GAP_CONN_PARAM_UPDATE_STATUS_SUCCESS:
        {
            update_conn_info(conn_id);
            (void)app_event_publish(EVT_BLE_CONN_PARAM, &hmi_conn_info, sizeof hmi_conn_info);
            APP_PRINT_INFO3("app_handle_conn_param_update_evt update success:conn_interval 0x%x, conn_latency 0x%x, conn_supervision_timeout 0x%x",
                            hmi_conn_info.conn_interval, hmi_conn_info.conn_latency, hmi_conn_info.conn_supervision_timeout);
        }
        break;

    case GAP_CONN_PARAM_UPDATE_STATUS_FAIL:
        {
            APP_PRINT_ERROR1("app_handle_conn_param_update_evt update failed: cause 0x%x", cause);
        }
        break;

    case GAP_CONN_PARAM_UPDATE_STATUS_PENDING:
        {
            APP_PRINT_INFO0("app_handle_conn_param_update_evt update pending.");
        }
        break;

    default:
        break;
    }
}


void app_handle_gap_msg(T_IO_MSG *p_gap_msg)
{
    T_LE_GAP_MSG gap_msg;
    uint8_t conn_id;
    memcpy(&gap_msg, &p_gap_msg->u.param, sizeof(p_gap_msg->u.param));
    APP_PRINT_TRACE1("app_handle_gap_msg: subtype %d", p_gap_msg->subtype);
    switch (p_gap_msg->subtype)
    {
    case GAP_MSG_LE_DEV_STATE_CHANGE:
        {
            app_handle_dev_state_evt(gap_msg.msg_data.gap_dev_state_change.new_state,
                                     gap_msg.msg_data.gap_dev_state_change.cause);
        }
        break;

    case GAP_MSG_LE_CONN_STATE_CHANGE:
        {
            app_handle_conn_state_evt(gap_msg.msg_data.gap_conn_state_change.conn_id,
                                      (T_GAP_CONN_STATE)gap_msg.msg_data.gap_conn_state_change.new_state,
                                      gap_msg.msg_data.gap_conn_state_change.disc_cause);
        }
        break;

    case GAP_MSG_LE_CONN_MTU_INFO:
        {
            app_handle_conn_mtu_info_evt(gap_msg.msg_data.gap_conn_mtu_info.conn_id,
                                         gap_msg.msg_data.gap_conn_mtu_info.mtu_size);
        }
        break;

    case GAP_MSG_LE_CONN_PARAM_UPDATE:
        {
            app_handle_conn_param_update_evt(gap_msg.msg_data.gap_conn_param_update.conn_id,
                                             gap_msg.msg_data.gap_conn_param_update.status,
                                             gap_msg.msg_data.gap_conn_param_update.cause);
        }
        break;

    case GAP_MSG_LE_AUTHEN_STATE_CHANGE:
        {
            app_handle_authen_state_evt(gap_msg.msg_data.gap_authen_state.conn_id,
                                        gap_msg.msg_data.gap_authen_state.new_state,
                                        gap_msg.msg_data.gap_authen_state.status);
        }
        break;

    case GAP_MSG_LE_BOND_JUST_WORK:
        {
            conn_id = gap_msg.msg_data.gap_bond_just_work_conf.conn_id;
            le_bond_just_work_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
            APP_PRINT_INFO0("GAP_MSG_LE_BOND_JUST_WORK");
        }
        break;

    case GAP_MSG_LE_BOND_PASSKEY_DISPLAY:
        {
            uint32_t display_value = 0;
            conn_id = gap_msg.msg_data.gap_bond_passkey_display.conn_id;
            le_bond_get_display_key(conn_id, &display_value);
            APP_PRINT_INFO1("GAP_MSG_LE_BOND_PASSKEY_DISPLAY:passkey %d", display_value);
            le_bond_passkey_display_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
        }
        break;

    case GAP_MSG_LE_BOND_USER_CONFIRMATION:
        {
            uint32_t display_value = 0;
            conn_id = gap_msg.msg_data.gap_bond_user_conf.conn_id;
            le_bond_get_display_key(conn_id, &display_value);
            APP_PRINT_INFO1("GAP_MSG_LE_BOND_USER_CONFIRMATION: passkey %d", display_value);
            le_bond_user_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
        }
        break;

    case GAP_MSG_LE_BOND_PASSKEY_INPUT:
        {
            uint32_t passkey = 888888;
            conn_id = gap_msg.msg_data.gap_bond_passkey_input.conn_id;
            APP_PRINT_INFO1("GAP_MSG_LE_BOND_PASSKEY_INPUT: conn_id %d", conn_id);
            le_bond_passkey_input_confirm(conn_id, passkey, GAP_CFM_CAUSE_ACCEPT);
        }
        break;

    case GAP_MSG_LE_BOND_OOB_INPUT:
        {
            uint8_t oob_data[GAP_OOB_LEN] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            conn_id = gap_msg.msg_data.gap_bond_oob_input.conn_id;
            APP_PRINT_INFO0("GAP_MSG_LE_BOND_OOB_INPUT");
            le_bond_set_param(GAP_PARAM_BOND_OOB_DATA, GAP_OOB_LEN, oob_data);
            le_bond_oob_input_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
        }
        break;

    default:
        APP_PRINT_ERROR1("app_handle_gap_msg: unknown subtype %d", p_gap_msg->subtype);
        break;
    }
}


void hmi_handle_bt_io_msg(T_IO_MSG io_msg)
{
    uint16_t msg_type = io_msg.type;
    APP_PRINT_TRACE1("app_handle_io_msg %d", msg_type);
    switch (msg_type)
    {
    case IO_MSG_TYPE_BT_STATUS:
        {
            app_handle_gap_msg(&io_msg);
            ble_slist_t *node;
            for (node = ble_slist_first(&(gap_msg_list.slist)); node; node = ble_slist_next(node))
            {
                T_LE_MSG_CBACK_ITEM *p_item = ble_container_of(node, T_LE_MSG_CBACK_ITEM, slist);
                p_item->cback(&io_msg);
            }
        }
        break;
    default:
        break;
    }
}


/*============================================================================*
 *                              BLE Connection Info
 *============================================================================*/

static void update_conn_info(uint8_t conn_id)
{
    le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &hmi_conn_info.conn_interval, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_LATENCY, &hmi_conn_info.conn_latency, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &hmi_conn_info.conn_supervision_timeout, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &hmi_conn_info.conn_mtu_size, conn_id);
}







