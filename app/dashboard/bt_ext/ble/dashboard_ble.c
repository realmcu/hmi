/*
 * Copyright (c) 2025 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Dashboard BLE subsystem — bluetooth_ext / bee-stack BLE peripheral.
 *
 * Initialises the BLE GAP/GATT stack on RTL8761B, configures advertising as
 * "AMEBAG2_BT_EX", and exposes HID Consumer Control via GATT notifications.
 * The task entry and message loop are owned by the coordinator
 * (dashboard_bt_ext.c).
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>
#include "ameba_soc.h"
#include "os_wrapper.h"
#include "os_mem.h"
#include "os_msg.h"
#include "os_task.h"

#include "bte.h"
#include "gap.h"
#include "gap_le.h"
#include "gap_adv.h"
#include "gap_msg.h"
#include "gap_callback_le.h"
#include "gap_bond_le.h"
#include "trace_app.h"
#include "app_msg.h"
#include "profile_server.h"

#include "dashboard_ble.h"
#include "bt_ext_hids_cc.h"
#include "../dashboard_bt_ext.h"

static const char *const TAG = "BLEEXT";

#define MAX_NUMBER_OF_GAP_MSG       0x20
#define MAX_NUMBER_OF_IO_MSG        0x40

/* MITM pairing uses DisplayOnly IO capability + this fixed passkey */
#define BT_EXT_FIXED_PASSKEY        123456

static T_SERVER_ID g_hids_cc_id = 0xFF;

/* Connection id of the current link. 0xFF means "no active connection". */
static uint8_t g_ble_conn_id = 0xFF;

static uint8_t ble_adv_data[] = {
	0x02,
	GAP_ADTYPE_FLAGS,
	GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
	0x03,
	GAP_ADTYPE_16BIT_COMPLETE,
	LO_WORD(0x1812),
	HI_WORD(0x1812),
	0x0E,
	GAP_ADTYPE_LOCAL_NAME_COMPLETE,
	'A', 'M', 'E', 'B', 'A', 'G', '2', '_', 'B', 'T', '_', 'E', 'X',
};

static uint8_t ble_scan_rsp_data[] = {
	0x03,
	GAP_ADTYPE_APPEARANCE,
	LO_WORD(GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE),
	HI_WORD(GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE),
};

static void app_ble_set_adv_params(void)
{
	uint8_t  adv_evt_type = GAP_ADTYPE_ADV_IND;
	uint8_t  adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC;
	uint8_t  adv_direct_addr[GAP_BD_ADDR_LEN] = {0};
	uint8_t  adv_chann_map = GAP_ADVCHAN_ALL;
	uint8_t  adv_filter_policy = GAP_ADV_FILTER_ANY;
	uint16_t adv_int_min = 32;
	uint16_t adv_int_max = 32;

	le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);
	le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR_TYPE, sizeof(adv_direct_type), &adv_direct_type);
	le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR, sizeof(adv_direct_addr), adv_direct_addr);
	le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);
	le_adv_set_param(GAP_PARAM_ADV_FILTER_POLICY, sizeof(adv_filter_policy), &adv_filter_policy);
	le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(adv_int_min), &adv_int_min);
	le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(adv_int_max), &adv_int_max);
	le_adv_set_param(GAP_PARAM_ADV_DATA, sizeof(ble_adv_data), ble_adv_data);
	le_adv_set_param(GAP_PARAM_SCAN_RSP_DATA, sizeof(ble_scan_rsp_data), ble_scan_rsp_data);
}

/* Human-readable name for informational GAP callback events */
static const char *gap_cb_type_str(uint8_t cb_type)
{
	switch (cb_type) {
	case GAP_MSG_LE_DATA_LEN_CHANGE_INFO:
		return "DATA_LEN_CHANGE_INFO";
	case GAP_MSG_LE_CONN_UPDATE_IND:
		return "CONN_UPDATE_IND";
	case GAP_MSG_LE_CREATE_CONN_IND:
		return "CREATE_CONN_IND";
	case GAP_MSG_LE_PHY_UPDATE_INFO:
		return "PHY_UPDATE_INFO";
	case GAP_MSG_LE_REMOTE_FEATS_INFO:
		return "REMOTE_FEATS_INFO";
	case GAP_MSG_LE_READ_REMOTE_VERSION:
		return "READ_REMOTE_VERSION";
	case GAP_MSG_LE_BOND_MODIFY_INFO:
		return "BOND_MODIFY_INFO";
	case GAP_MSG_LE_SCAN_INFO:
		return "SCAN_INFO";
	default:
		return "unknown";
	}
}

/* ---- GAP callback -------------------------------------------------------- */
static T_APP_RESULT app_ble_gap_cb(uint8_t cb_type, void *p_cb_data)
{
	T_APP_RESULT result = APP_RESULT_SUCCESS;
	T_LE_CB_DATA cb_data;

	memcpy(&cb_data, p_cb_data, sizeof(T_LE_CB_DATA));

	switch (cb_type) {
	case GAP_MSG_LE_GAP_STATE_MSG:
		dashboard_ble_gap_handler((T_IO_MSG *)cb_data.p_gap_state_msg);
		break;

	case GAP_MSG_LE_BOND_MODIFY_INFO: {
		T_LE_BOND_MODIFY_INFO *p_bond = cb_data.p_le_bond_modify_info;
		const char *op;

		switch (p_bond->type) {
		case LE_BOND_ADD:
			op = "ADD";
			break;
		case LE_BOND_DELETE:
			op = "DELETE";
			break;
		case LE_BOND_CLEAR:
			op = "CLEAR";
			break;
		case LE_BOND_FULL:
			op = "FULL";
			break;
		case LE_BOND_KEY_MISSING:
			op = "KEY_MISSING";
			break;
		default:
			op = "UNKNOWN";
			break;
		}

		if (p_bond->p_entry != NULL) {
			uint8_t *a = p_bond->p_entry->remote_bd.addr;
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> BOND %s: idx=%d, addr=%02x:%02x:%02x:%02x:%02x:%02x, addr_type=%d <<<\r\n",
				op, p_bond->p_entry->idx,
				a[5], a[4], a[3], a[2], a[1], a[0],
				p_bond->p_entry->remote_bd.remote_bd_type);
		} else {
			RTK_LOGS(TAG, RTK_LOG_INFO, ">>> BOND %s (no entry) <<<\r\n", op);
		}
		break;
	}

	default:
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"gap_cb: %s (cb_type=0x%02x)\r\n", gap_cb_type_str(cb_type), cb_type);
		break;
	}

	return result;
}

/* ---- IO / GAP message handling ------------------------------------------- */

/*
 * Handle a GAP status message (IO_MSG_TYPE_BT_STATUS, subtype GAP_MSG_LE_*).
 * The 4-byte payload is the T_LE_GAP_MSG data, read here by field offset.
 */
void dashboard_ble_gap_handler(T_IO_MSG *p_io_msg)
{
	uint8_t *p = (uint8_t *)&p_io_msg->u.param;

	switch (p_io_msg->subtype) {
	case 0x01: { /* GAP_MSG_LE_DEV_STATE_CHANGE */
		uint8_t st = p[0];
		uint8_t init  = st & 0x01;
		uint8_t adv_sub = (st >> 1) & 0x01;
		uint8_t adv  = (st >> 2) & 0x03;
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"dev_state: init=%d adv=%d adv_sub=%d\r\n",
			init, adv, adv_sub);
		if (adv == 2) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> BLE ADVERTISING STARTED <<<\r\n");
		} else if (adv == 0 && adv_sub == 1) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> ADV STOPPED: connection created <<<\r\n");
		}
		break;
	}
	case 0x02: { /* GAP_MSG_LE_CONN_STATE_CHANGE */
		uint8_t conn_id = p[0];
		uint8_t new_st  = p[1];
		if (new_st == 1) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> BLE CONNECTING: conn_id=%d <<<\r\n", conn_id);
		} else if (new_st == 2) {
			g_ble_conn_id = conn_id;
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> BLE CONNECTED: conn_id=%d <<<\r\n", conn_id);
		} else if (new_st == 0) {
			g_ble_conn_id = 0xFF;
			uint16_t reason = p[2] | (p[3] << 8);
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> BLE DISCONNECTED: conn_id=%d reason=0x%04x, restarting adv <<<\r\n",
				conn_id, reason);
			le_adv_start();
		}
		break;
	}
	case 0x03: { /* GAP_MSG_LE_CONN_PARAM_UPDATE */
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"conn_param_update: conn_id=%d status=%d\r\n",
			p[0], p[1]);
		break;
	}
	case 0x04: { /* GAP_MSG_LE_CONN_MTU_INFO */
		uint16_t mtu = p[2] | (p[3] << 8);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"conn_mtu: conn_id=%d mtu=%d\r\n", p[0], mtu);
		break;
	}
	case GAP_MSG_LE_AUTHEN_STATE_CHANGE: { /* 0x05 */
		uint8_t conn_id = p[0];
		uint8_t new_state = p[1];
		uint16_t status = p[2] | (p[3] << 8);
		if (new_state == GAP_AUTHEN_STATE_STARTED) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> PAIRING STARTED, conn_id=%d <<<\r\n", conn_id);
		} else if (new_state == GAP_AUTHEN_STATE_COMPLETE) {
			if (status == 0)
				RTK_LOGS(TAG, RTK_LOG_INFO,
					">>> PAIRING SUCCESS, conn_id=%d <<<\r\n", conn_id);
			else
				RTK_LOGS(TAG, RTK_LOG_ERROR,
					">>> PAIRING FAILED, conn_id=%d, status=0x%04x <<<\r\n",
					conn_id, status);
		}
		break;
	}
	case GAP_MSG_LE_BOND_PASSKEY_DISPLAY: { /* 0x06 */
		uint8_t conn_id = p[0];
		uint32_t passkey = 0;
		if (le_bond_get_display_key(conn_id, &passkey) == GAP_CAUSE_SUCCESS)
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> PAIRING PASSKEY: %06u (enter on peer), conn_id=%d <<<\r\n",
				(unsigned int)passkey, conn_id);
		le_bond_passkey_display_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
		break;
	}
	case GAP_MSG_LE_BOND_PASSKEY_INPUT: { /* 0x07 */
		uint8_t conn_id = p[0];
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"passkey input requested, using fixed passkey, conn_id=%d\r\n", conn_id);
		le_bond_passkey_input_confirm(conn_id, BT_EXT_FIXED_PASSKEY,
									  GAP_CFM_CAUSE_ACCEPT);
		break;
	}
	case GAP_MSG_LE_BOND_OOB_INPUT: { /* 0x08 */
		uint8_t conn_id = p[0];
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"OOB input requested (not supported), conn_id=%d\r\n", conn_id);
		le_bond_oob_input_confirm(conn_id, GAP_CFM_CAUSE_REJECT);
		break;
	}
	case GAP_MSG_LE_BOND_USER_CONFIRMATION: { /* 0x09 */
		uint8_t conn_id = p[0];
		uint32_t passkey = 0;
		if (le_bond_get_display_key(conn_id, &passkey) == GAP_CAUSE_SUCCESS)
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"numeric comparison value: %06u, conn_id=%d (auto-accept)\r\n",
				(unsigned int)passkey, conn_id);
		le_bond_user_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
		break;
	}
	case GAP_MSG_LE_BOND_JUST_WORK: { /* 0x0A */
		uint8_t conn_id = p[0];
		le_bond_just_work_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"just-work confirm, conn_id=%d\r\n", conn_id);
		break;
	}
	default:
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"io_msg: subtype=0x%02x param=0x%08x\r\n",
			p_io_msg->subtype, (unsigned int)p_io_msg->u.param);
		break;
	}
}

/* ---- key event handling (BLE HID) ---------------------------------------- */

void dashboard_ble_key_handler(key_event_t event)
{
	if (g_ble_conn_id == 0xFF) {
		RTK_LOGS(TAG, RTK_LOG_WARN,
			"ble key event %d ignored: no active connection\r\n", event);
		return;
	}

	switch (event) {
	case KEY_NEXT_TRACK_PRESS:
		bt_ext_hids_cc_next_track(g_ble_conn_id, true);
		break;
	case KEY_NEXT_TRACK_RELEASE:
		bt_ext_hids_cc_next_track(g_ble_conn_id, false);
		break;
	case KEY_PREV_TRACK_PRESS:
		bt_ext_hids_cc_prev_track(g_ble_conn_id, true);
		break;
	case KEY_PREV_TRACK_RELEASE:
		bt_ext_hids_cc_prev_track(g_ble_conn_id, false);
		break;
	case KEY_PLAY_PAUSE_PRESS:
		bt_ext_hids_cc_play_pause(g_ble_conn_id, true);
		break;
	case KEY_PLAY_PAUSE_RELEASE:
		bt_ext_hids_cc_play_pause(g_ble_conn_id, false);
		break;
	default:
		break;
	}
}

/* ---- GATT server callback ------------------------------------------------ */
static T_APP_RESULT app_profile_cb(T_SERVER_ID service_id, void *p_data)
{
	T_APP_RESULT result = APP_RESULT_SUCCESS;
	T_SERVER_APP_CB_DATA *p_param = (T_SERVER_APP_CB_DATA *)p_data;

	(void)service_id;

	switch (p_param->eventId) {
	case PROFILE_EVT_SRV_REG_COMPLETE:
		RTK_LOGS(TAG, RTK_LOG_INFO, "GATT services register complete, result %d\r\n",
			p_param->event_data.service_reg_result);
		break;
	case PROFILE_EVT_SEND_DATA_COMPLETE:
		break;
	default:
		break;
	}

	return result;
}

/* ---- BLE-specific initialisation ----------------------------------------- */

int dashboard_ble_init(void **p_evt_queue, void **p_io_queue)
{
	uint8_t link_num;

	RTK_LOGS(TAG, RTK_LOG_INFO, "BLE EXT init start\r\n");

	os_msg_queue_create(p_io_queue, MAX_NUMBER_OF_IO_MSG, sizeof(T_IO_MSG));
	os_msg_queue_create(p_evt_queue,
		MAX_NUMBER_OF_GAP_MSG + MAX_NUMBER_OF_IO_MSG, sizeof(uint8_t));
	if (!*p_io_queue || !*p_evt_queue) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "msg queue create failed\r\n");
		return -1;
	}

	link_num = le_get_max_link_num();
	if (link_num > 3)
		link_num = 3;

	if (!le_gap_init(link_num)) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "le_gap_init failed\r\n");
		return -1;
	}

	le_register_app_cb(app_ble_gap_cb);

	/* Deliver GAP device/connection/auth/bond messages through app_ble_gap_cb
	 * (as GAP_MSG_LE_GAP_STATE_MSG) instead of posting them to the io queue. */
	le_gap_msg_info_way(false);

	{
		uint8_t  device_name[GAP_DEVICE_NAME_LEN] = "AMEBAG2_BT_EX";
		uint16_t appearance = GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE;
		le_set_gap_param(GAP_PARAM_DEVICE_NAME, GAP_DEVICE_NAME_LEN, device_name);
		le_set_gap_param(GAP_PARAM_APPEARANCE, sizeof(appearance), &appearance);
	}

	/* GAP Bond Manager */
	{
		uint8_t  auth_pair_mode = GAP_PAIRING_MODE_PAIRABLE;
		uint16_t auth_flags = GAP_AUTHEN_BIT_BONDING_FLAG
							| GAP_AUTHEN_BIT_MITM_FLAG
							| GAP_AUTHEN_BIT_SC_FLAG;
		uint8_t  auth_io_cap = GAP_IO_CAP_DISPLAY_YES_NO;
		uint8_t  auth_oob = false;
		uint8_t  auth_use_fix_passkey = false;
		uint8_t  auth_sec_req_enable = true;
		uint16_t auth_sec_req_flags = GAP_AUTHEN_BIT_BONDING_FLAG;

		gap_set_param(GAP_PARAM_BOND_PAIRING_MODE, sizeof(auth_pair_mode), &auth_pair_mode);
		gap_set_param(GAP_PARAM_BOND_AUTHEN_REQUIREMENTS_FLAGS, sizeof(auth_flags), &auth_flags);
		gap_set_param(GAP_PARAM_BOND_IO_CAPABILITIES, sizeof(auth_io_cap), &auth_io_cap);
		gap_set_param(GAP_PARAM_BOND_OOB_ENABLED, sizeof(auth_oob), &auth_oob);

		le_bond_set_param(GAP_PARAM_BOND_FIXED_PASSKEY_ENABLE, sizeof(auth_use_fix_passkey),
						  &auth_use_fix_passkey);
		le_bond_set_param(GAP_PARAM_BOND_SEC_REQ_ENABLE, sizeof(auth_sec_req_enable),
						  &auth_sec_req_enable);
		le_bond_set_param(GAP_PARAM_BOND_SEC_REQ_REQUIREMENT, sizeof(auth_sec_req_flags),
						  &auth_sec_req_flags);
	}

	app_ble_set_adv_params();

	/* GATT server: register the HID Consumer Control profile */
	server_init(1);
	g_hids_cc_id = bt_ext_hids_cc_add_service(app_profile_cb);
	if (g_hids_cc_id == 0xFF) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "HID CC service register failed\r\n");
		return -1;
	}
	server_register_app_cb(app_profile_cb);

	RTK_LOGS(TAG, RTK_LOG_INFO, "BLE EXT init done\r\n");
	return 0;
}

void dashboard_ble_start(void)
{
	le_adv_start();
}

#endif /* CONFIG_BT_EXT */
