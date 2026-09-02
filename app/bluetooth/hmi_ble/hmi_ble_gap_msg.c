
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
#include "hmi_ble_conn.h"
#include "app_ota_service.h"
#include "hmi_br_edr_gap_init.h" /* hmi_br_edr_set_radio_enabled() */
#if 0 /* V1.2 migration: P2P sender parked */
#include "hmi_ble_central.h"
#endif

#define HMI_BLE_INVALID_CONN_ID     0xFFu

static T_GAP_DEV_STATE gap_dev_state = {0, 0, 0, 0};                 /**< GAP device state */
static T_GAP_CONN_STATE gap_conn_state = GAP_CONN_STATE_DISCONNECTED; /**< GAP connection state */
static hmi_ble_conn_info_t hmi_conn_info = {0, 0, 0, 0};             /**< Cached BLE connection info */
static uint8_t s_conn_id = HMI_BLE_INVALID_CONN_ID;                  /**< Active link, for le_disconnect() */

static T_LE_MSG_CBACK_ITEM gap_msg_list = {NULL, NULL};

static void update_conn_info(uint8_t conn_id);

/* Who owns the current link: true if WE initiated it (central/master, file-send
 * mode), false if the peer connected to us (peripheral/slave, the phone).
 *
 * This is NOT hmi_ble_central_is_active(), and the difference bites: is_active()
 * is true for the whole of CEN_SCANNING, so with a device list open on screen a
 * phone's link would be misrouted into the central handler and
 * app_ota_glue_link_disconnected() would never run.  The link role from
 * le_get_conn_info() is the authoritative discriminator.
 *
 * Latched at CONNECTED rather than queried at DISCONNECTED, because by the time
 * the disconnect arrives the link is gone and le_get_conn_info() has nothing
 * left to report. */
static bool s_link_is_master = false;

/* le_adv_start() may be rejected (cause 0x2 GAP_CAUSE_INVALID_STATE) while a
 * link is still tearing down: right after a central/master disconnect the
 * controller has not finished releasing the link.  (A slave/peripheral
 * disconnect never hits this, which is why the old direct le_adv_start()
 * worked there.)  This transient is NOT reflected in T_GAP_DEV_STATE, so a
 * dev-state-change retry never fires -- retry on a short one-shot timer. */
static void    *s_adv_retry_timer     = NULL;
static uint8_t  s_adv_retry_cnt       = 0;

/* Advertising gate.  Closed by hmi_ble_gap_set_adv_enabled(false) -- e.g. the UI
 * power-off path -- so that the three implicit "back to advertising" transitions
 * (GAP_INIT_STATE_STACK_READY, GAP_CONN_STATE_DISCONNECTED, and the retry timer
 * below) cannot bring the radio back up behind the user's back. */
static bool     s_adv_gate_closed     = false;

#define ADV_RETRY_TIMER_ID   1u
#define ADV_RETRY_MS         200u
#define ADV_RETRY_MAX        10u    /* ~2s worst case */

static void adv_retry_cancel(void)
{
    s_adv_retry_cnt = 0;
    if (s_adv_retry_timer != NULL)
    {
        os_timer_stop(&s_adv_retry_timer);
    }
}

static void adv_retry_timer_cb(void *p_handle)
{
    (void)p_handle;
    /* Re-attempt from the timer task; le_adv_start() only posts to the stack. */
    hmi_ble_gap_start_adv();
}

void hmi_ble_gap_start_adv(void)
{
    if (s_adv_gate_closed)
    {
        /* Advertising was deliberately switched off; swallow the restart. */
        adv_retry_cancel();
        APP_PRINT_INFO0("hmi_ble_gap_start_adv: suppressed, adv gate closed");
        return;
    }

    uint8_t adv_st = gap_dev_state.gap_adv_state;
    if (adv_st == GAP_ADV_STATE_ADVERTISING || adv_st == GAP_ADV_STATE_START)
    {
        /* Already advertising / starting (e.g. a master connection did not stop
         * our advertising) -- nothing to do. */
        adv_retry_cancel();
        return;
    }

    T_GAP_CAUSE cause = le_adv_start();
    if (cause == GAP_CAUSE_SUCCESS)
    {
        adv_retry_cancel();
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

void hmi_ble_gap_set_adv_enabled(bool enable)
{
    if (enable)
    {
        s_adv_gate_closed = false;
        APP_PRINT_INFO0("hmi_ble_gap_set_adv_enabled: gate open, restarting adv");
        hmi_ble_gap_start_adv();
        return;
    }

    s_adv_gate_closed = true;

    /* Order matters: kill the retry timer first.  Its callback calls
     * hmi_ble_gap_start_adv(), and although the gate now swallows that, cancelling
     * here also releases the timer for the next open/close cycle. */
    adv_retry_cancel();

    /* Drop an active link as well.  A connection wakes the radio every connection
     * interval (30ms after the update in app_handle_conn_state_evt), which costs
     * considerably more than the 200ms advertising we are about to stop.  The
     * DISCONNECTED handler calls hmi_ble_gap_start_adv(), which the closed gate
     * now suppresses. */
    if (gap_conn_state == GAP_CONN_STATE_CONNECTED && s_conn_id != HMI_BLE_INVALID_CONN_ID)
    {
        APP_PRINT_INFO1("hmi_ble_gap_set_adv_enabled: disconnecting conn_id %d", s_conn_id);
        le_disconnect(s_conn_id);
    }

    uint8_t adv_st = gap_dev_state.gap_adv_state;
    if (adv_st == GAP_ADV_STATE_ADVERTISING || adv_st == GAP_ADV_STATE_START)
    {
        T_GAP_CAUSE cause = le_adv_stop();
        APP_PRINT_INFO2("hmi_ble_gap_set_adv_enabled: le_adv_stop adv_state %d, cause 0x%x",
                        adv_st, cause);
    }
    else
    {
        APP_PRINT_INFO1("hmi_ble_gap_set_adv_enabled: gate closed, adv already off (state %d)",
                        adv_st);
    }
}

bool hmi_ble_request_adv_enable(bool enable)
{
    T_IO_MSG msg;

    msg.type    = IO_MSG_TYPE_WRISTBNAD; // can't find more suitable type
    msg.subtype = enable ? IO_MSG_START_BLE_ADV : IO_MSG_STOP_BLE_ADV;
    msg.u.param = 0;

    return hmi_send_msg_to_bt_task(&msg);
}

uint8_t hmi_ble_gap_get_adv_state(void)
{
    return gap_dev_state.gap_adv_state;
}

bool hmi_ble_adv_is_idle(void)
{
    return gap_dev_state.gap_adv_state == GAP_ADV_STATE_IDLE;
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
        /* Let the central module notice a scan that genuinely reached IDLE, so the
         * next start_scan re-issues le_scan_start() instead of reusing a scan that
         * is no longer running.  On this stack that effectively never happens (the
         * scan wedges at START); it is a self-heal, not the normal path. */
        hmi_ble_central_handle_scan_state(new_state.gap_scan_state);
    }

    /* Second half of the serialized adv-stop -> scan-start, for the code path that
     * stops advertising first.  start_scan() deliberately does NOT stop adv, so
     * this too is currently a no-op kept for the state machine's completeness. */
    hmi_ble_central_handle_adv_state(new_state.gap_adv_state);

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
            /* Route by who owned the link, using the role latched at CONNECTED --
             * not hmi_ble_central_is_active(), which is also true for a mere open
             * scan and would send a phone's disconnect down the central path (see
             * s_link_is_master). */
            if (s_link_is_master)
            {
                /* We owned this link as GATT client (file-send mode). */
                hmi_ble_central_handle_disconnected(conn_id, disc_cause);
            }
            else
            {
                app_ota_glue_link_disconnected(conn_id, disc_cause);
            }
            s_link_is_master = false;
            memset(&hmi_conn_info, 0, sizeof(hmi_conn_info));
            s_conn_id = HMI_BLE_INVALID_CONN_ID;
            /* Suppressed while the adv gate is closed (UI power-off). */
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

            /* Latch who owns this link while it still exists (see s_link_is_master).
             * Fall back to "peer connected to us" if the query fails: the phone is
             * the overwhelmingly common case, and taking the receiver branch on a
             * central link merely skips discovery, whereas taking the central
             * branch on a phone link breaks the whole receive path. */
            T_GAP_CONN_INFO conn_info;
            if (le_get_conn_info(conn_id, &conn_info))
            {
                s_link_is_master = (conn_info.role == GAP_LINK_ROLE_MASTER);
            }
            else
            {
                s_link_is_master = false;
                APP_PRINT_WARN1("le_get_conn_info failed for conn_id %d, assuming slave", conn_id);
            }
            APP_PRINT_INFO1("GAP_CONN_STATE_CONNECTED: link role %s",
                            TRACE_STRING(s_link_is_master ? "master(central)" : "slave(peripheral)"));

            if (s_link_is_master)
            {
                /* We initiated this link as GATT client -> start discovery.
                 * Reachable with the sender parked: handle_connected() only uses the
                 * ROM client API, so discovery runs and CEN_READY is reached; just
                 * hmi_ble_central_send_file() is absent. */
                hmi_ble_central_handle_connected(conn_id);
            }
            else
            {
                s_conn_id = conn_id;
                update_conn_info(conn_id);
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

    /* App-side commands.  These arrive from other tasks (GUI, protocol) which
     * must not touch GAP directly -- le_adv_start()/le_adv_stop() only post to
     * the stack and expect to be driven from the BT task. */
    case IO_MSG_TYPE_WRISTBNAD:
        {
            switch (io_msg.subtype)
            {
            case IO_MSG_START_BLE_ADV:
                hmi_ble_gap_set_adv_enabled(true);
                break;

            case IO_MSG_STOP_BLE_ADV:
                hmi_ble_gap_set_adv_enabled(false);
                break;

#ifdef CONFIG_RTK_BT_BREDR
            case IO_MSG_TYPE_EVENT_BUS:
                hmi_br_edr_set_radio_enabled(false);
                break;
#endif

            default:
                APP_PRINT_WARN1("hmi_handle_bt_io_msg: unhandled app subtype %d", io_msg.subtype);
                break;
            }
        }
        break;

    default:
        break;
    }
}


/*============================================================================*
 *                              BLE Connection Info (shared with protocol)
 *============================================================================*/

static void update_conn_info(uint8_t conn_id)
{
    le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &hmi_conn_info.conn_interval, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_LATENCY, &hmi_conn_info.conn_latency, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &hmi_conn_info.conn_supervision_timeout, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &hmi_conn_info.conn_mtu_size, conn_id);
}

bool hmi_ble_get_conn_info(hmi_ble_conn_info_t *info)
{
    if (!info)
    {
        return false;
    }

    if (gap_conn_state != GAP_CONN_STATE_CONNECTED)
    {
        memset(info, 0, sizeof(*info));
        return false;
    }

    *info = hmi_conn_info;
    return true;
}







