#include "ameba_soc.h"
#include "os_wrapper.h"

/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * Licensed under the Realtek License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License from Realtek
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>

#include "os_wrapper.h"

#include "wifi_api.h"
#include "wifi_api_types.h"

#include <bt_api_config.h>
#include <rtk_bt_def.h>
#include <rtk_bt_common.h>
#include <rtk_bt_device.h>
#include <rtk_bt_gap.h>
#include <rtk_bt_le_gap.h>
#include <rtk_bt_att_defs.h>
#include <rtk_bt_gatts.h>

#include <rtk_service_config.h>
#include <rtk_bas.h>
#include <rtk_hrs.h>
#include <rtk_simple_ble_service.h>
#include <rtk_dis.h>
#include <rtk_ias.h>
#include "dashboard_hid_service/dashboard_hids_cc.h"
#include <rtk_gls.h>
#include <rtk_long_uuid_service.h>
#include <bt_utils.h>

#include "dashboard_ble.h"

#include "dashboard_key.h"


#define RTK_BT_DEV_NAME "RTK_BT_DASHBOARD"

static uint16_t g_ble_conn_handle = 0xFFFF;

/* Key events from io_peripheral are pushed here by ble_key_send_hook() (runs in
 * the button IRQ / CLI context) and drained by dash_board_ble_task(), which can
 * safely call the rtk_bt notify APIs. */
static rtos_queue_t g_ble_key_queue = NULL;

static const char *TAG = "DASHBOARD";

static uint8_t adv_data[] = {
	0x02,
	RTK_BT_LE_GAP_ADTYPE_FLAGS,
	RTK_BT_LE_GAP_ADTYPE_FLAGS_GENERAL | RTK_BT_LE_GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
	0x03,
	RTK_BT_LE_GAP_ADTYPE_SERVICES_LIST_16BIT,
	LO_WORD(0x1812),
	HI_WORD(0x1812),
	0x11,
	RTK_BT_LE_GAP_ADTYPE_LOCAL_NAME_COMPLETE,
	'R', 'T', 'K', '_', 'B', 'T', '_', 'D', 'A', 'S', 'H', 'B', 'O', 'A', 'R', 'D',
};

static uint8_t scan_rsp_data[] = {
	0x3,
	RTK_BT_LE_GAP_ADTYPE_APPEARANCE,
	LO_WORD(RTK_BT_LE_GAP_APPEARANCE_GENERIC_HID),
	HI_WORD(RTK_BT_LE_GAP_APPEARANCE_GENERIC_HID),
};

static rtk_bt_le_adv_param_t def_adv_param = {
	.interval_min = 200,
	.interval_max = 250,
	.type = RTK_BT_LE_ADV_TYPE_IND,
	.own_addr_type = RTK_BT_LE_ADDR_TYPE_PUBLIC,
	.peer_addr = {
		.type = (rtk_bt_le_addr_type_t)0,
		.addr_val = {0},
	},
	.channel_map = RTK_BT_LE_ADV_CHNL_ALL,
	.filter_policy = RTK_BT_LE_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static rtk_bt_le_security_param_t sec_param = {
	.io_cap = RTK_IO_CAP_DISPLAY_YES_NO,
	.oob_data_flag = 0,
	.bond_flag = 1,
	.mitm_flag = 1,
	.sec_pair_flag = 1,
	.sec_pair_only_flag = 0,
	.use_fixed_key = 0,
	.fixed_key = 000000,
	.auto_sec_req = 0,
	.sign_key_flag = 0,
};

static void app_server_disconnect(uint16_t conn_handle)
{
	simple_ble_srv_disconnect(conn_handle);
	bas_disconnect(conn_handle);
	hrs_disconnect(conn_handle);
	gls_disconnect(conn_handle);
	dashboard_hid_cc_disconnect(conn_handle);
}

static void app_server_deinit(void)
{
	simple_ble_srv_status_deinit();
	bas_status_deinit();
	hrs_status_deinit();
	gls_status_deinit();
	dashboard_hid_cc_status_deinit();
}

static rtk_bt_evt_cb_ret_t peripheral_gap_app_callback(uint8_t evt_code, void *param, uint32_t len)
{
	(void)param;
	(void)len;
	rtk_bt_evt_cb_ret_t ret = RTK_BT_EVT_CB_OK;

	switch (evt_code) {
	default:
		BT_LOGE("[APP] Unknown common gap cb evt type: %d\r\n", evt_code);
		break;
	}

	return ret;
}

static rtk_bt_evt_cb_ret_t ble_peripheral_gap_app_callback(uint8_t evt_code, void *param, uint32_t len)
{
	(void)len;
	char le_addr[30] = {0};
	char *role;

	switch (evt_code) {
	case RTK_BT_LE_GAP_EVT_ADV_START_IND: {
		rtk_bt_le_adv_start_ind_t *adv_start_ind = (rtk_bt_le_adv_start_ind_t *)param;
		if (!adv_start_ind->err) {
			BT_LOGA("[APP] ADV started: adv_type %d  \r\n", adv_start_ind->adv_type);
		} else {
			BT_LOGE("[APP] ADV start failed, err 0x%x \r\n", adv_start_ind->err);
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_ADV_STOP_IND: {
		rtk_bt_le_adv_stop_ind_t *adv_stop_ind = (rtk_bt_le_adv_stop_ind_t *)param;
		if (!adv_stop_ind->err) {
			BT_LOGA("[APP] ADV stopped: reason 0x%x \r\n", adv_stop_ind->stop_reason);
		} else {
			BT_LOGE("[APP] ADV stop failed, err 0x%x \r\n", adv_stop_ind->err);
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_CONNECT_IND: {
		rtk_bt_le_conn_ind_t *conn_ind = (rtk_bt_le_conn_ind_t *)param;
		rtk_bt_le_addr_to_str(&(conn_ind->peer_addr), le_addr, sizeof(le_addr));
		if (!conn_ind->err) {
			g_ble_conn_handle = conn_ind->conn_handle;
			role = conn_ind->role ? "slave" : "master";
			BT_LOGA("[APP] Connected, handle: %d, role: %s, remote device: %s\r\n",
					conn_ind->conn_handle, role, le_addr);
		} else {
			BT_LOGE("[APP] Connection establish failed(err: 0x%x), remote device: %s\r\n",
					conn_ind->err, le_addr);
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_DISCONN_IND: {
		rtk_bt_le_disconn_ind_t *disconn_ind = (rtk_bt_le_disconn_ind_t *)param;
		g_ble_conn_handle = 0xFFFF;
		rtk_bt_le_addr_to_str(&(disconn_ind->peer_addr), le_addr, sizeof(le_addr));
		role = disconn_ind->role ? "slave" : "master";
		BT_LOGA("[APP] Disconnected, reason: 0x%x, handle: %d, role: %s, remote device: %s\r\n",
				disconn_ind->reason, disconn_ind->conn_handle, role, le_addr);

		rtk_bt_le_gap_dev_state_t dev_state;
		rtk_bt_le_adv_param_t adv_param = {0};
		if (rtk_bt_le_gap_get_dev_state(&dev_state) == RTK_BT_OK &&
			dev_state.gap_adv_state == RTK_BT_LE_ADV_STATE_IDLE) {
			memcpy(&adv_param, &def_adv_param, sizeof(rtk_bt_le_adv_param_t));
			BT_LOGA("[APP] Reconnect ADV starting, adv type:%d,  own_addr_type: %d, filter_policy: %d\r\n"
					, adv_param.type,  adv_param.own_addr_type, adv_param.filter_policy);
			BT_APP_PROCESS(rtk_bt_le_gap_start_adv(&adv_param));
		}
		app_server_disconnect(disconn_ind->conn_handle);
		break;
	}

	case RTK_BT_LE_GAP_EVT_CONN_UPDATE_IND: {
		rtk_bt_le_conn_update_ind_t *conn_update_ind =
			(rtk_bt_le_conn_update_ind_t *)param;
		if (conn_update_ind->err) {
			BT_LOGE("[APP] Update conn param failed, conn_handle: %d, err: 0x%x\r\n",
					conn_update_ind->conn_handle, conn_update_ind->err);
		} else {
			BT_LOGA("[APP] Conn param is updated, conn_handle: %d, conn_interval: 0x%x, "
					"conn_latency: 0x%x, supervision_timeout: 0x%x\r\n",
					conn_update_ind->conn_handle,
					conn_update_ind->conn_interval,
					conn_update_ind->conn_latency,
					conn_update_ind->supv_timeout);
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_REMOTE_CONN_UPDATE_REQ_IND: {
		rtk_bt_le_remote_conn_update_req_ind_t *rmt_update_req =
			(rtk_bt_le_remote_conn_update_req_ind_t *)param;
		BT_LOGA("[APP] Remote device request a change in conn param, conn_handle: %d, "
				"conn_interval_max: 0x%x, conn_interval_min: 0x%x, conn_latency: 0x%x, "
				"timeout: 0x%x. The host stack accept it.\r\n",
				rmt_update_req->conn_handle,
				rmt_update_req->conn_interval_max,
				rmt_update_req->conn_interval_min,
				rmt_update_req->conn_latency,
				rmt_update_req->supv_timeout);
		return RTK_BT_EVT_CB_ACCEPT;
	}

	case RTK_BT_LE_GAP_EVT_DATA_LEN_CHANGE_IND: {
		rtk_bt_le_data_len_change_ind_t *data_len_change =
			(rtk_bt_le_data_len_change_ind_t *)param;
		BT_LOGA("[APP] Data len is updated, conn_handle: %d, "
				"max_tx_octets: 0x%x, max_tx_time: 0x%x, "
				"max_rx_octets: 0x%x, max_rx_time: 0x%x\r\n",
				data_len_change->conn_handle,
				data_len_change->max_tx_octets,
				data_len_change->max_tx_time,
				data_len_change->max_rx_octets,
				data_len_change->max_rx_time);
		break;
	}

	case RTK_BT_LE_GAP_EVT_PHY_UPDATE_IND: {
		rtk_bt_le_phy_update_ind_t *phy_update_ind =
			(rtk_bt_le_phy_update_ind_t *)param;
		if (phy_update_ind->err) {
			BT_LOGE("[APP] Update PHY failed, conn_handle: %d, err: 0x%x\r\n",
					phy_update_ind->conn_handle, phy_update_ind->err);
		} else {
			BT_LOGA("[APP] PHY is updated, conn_handle: %d, tx_phy: %d, rx_phy: %d\r\n",
					phy_update_ind->conn_handle,
					phy_update_ind->tx_phy,
					phy_update_ind->rx_phy);
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_READ_REMOTE_VERSION_IND: {
		rtk_bt_le_read_remote_version_ind_t *rmt_ver = (rtk_bt_le_read_remote_version_ind_t *)param;
		if (rmt_ver->err) {
			BT_LOGE("[APP] Read remote version failed, conn_handle: %d, err: 0x%x\r\n",
					rmt_ver->conn_handle, rmt_ver->err);
		} else {
			BT_LOGA("[APP] Read remote version, conn_handle: %d, version: 0x%x, company_id: 0x%x, subversion: 0x%x\r\n",
					rmt_ver->conn_handle, rmt_ver->version, rmt_ver->company_id, rmt_ver->subversion);
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_AUTH_PAIRING_CONFIRM_IND: {
		rtk_bt_le_auth_pair_cfm_ind_t *pair_cfm_ind =
			(rtk_bt_le_auth_pair_cfm_ind_t *)param;
		BT_LOGA("[APP] Just work pairing need user to confirm, conn_handle: %d!\r\n",
				pair_cfm_ind->conn_handle);
		rtk_bt_le_pair_cfm_t pair_cfm_param = {0};
		uint16_t ret = 0;
		pair_cfm_param.conn_handle = pair_cfm_ind->conn_handle;
		pair_cfm_param.confirm = 1;
		ret = rtk_bt_le_sm_pairing_confirm(&pair_cfm_param);
		if (RTK_BT_OK == ret) {
			BT_LOGA("[APP] Just work pairing auto confirm succcess\r\n");
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_AUTH_PASSKEY_DISPLAY_IND: {
		rtk_bt_le_auth_key_display_ind_t *key_dis_ind =
			(rtk_bt_le_auth_key_display_ind_t *)param;
		BT_LOGA("[APP] Auth passkey display: %d, conn_handle:%d\r\n",
				key_dis_ind->passkey,
				key_dis_ind->conn_handle);
		break;
	}

	case RTK_BT_LE_GAP_EVT_AUTH_PASSKEY_INPUT_IND: {
		rtk_bt_le_auth_key_input_ind_t *key_input_ind =
			(rtk_bt_le_auth_key_input_ind_t *)param;
		BT_LOGA("[APP] Please input the auth passkey get from remote, conn_handle: %d\r\n",
				key_input_ind->conn_handle);
		break;
	}

	case RTK_BT_LE_GAP_EVT_AUTH_PASSKEY_CONFIRM_IND: {
		rtk_bt_le_auth_key_cfm_ind_t *key_cfm_ind =
			(rtk_bt_le_auth_key_cfm_ind_t *)param;
		BT_LOGA("[APP] Auth passkey confirm: %d, conn_handle: %d. "
				"Please comfirm if the passkeys are equal!\r\n",
				key_cfm_ind->passkey,
				key_cfm_ind->conn_handle);
		rtk_bt_le_auth_key_confirm_t key_cfm_param = {0};
		uint16_t ret = 0;
		key_cfm_param.conn_handle = key_cfm_ind->conn_handle;
		key_cfm_param.confirm = 1;
		ret = rtk_bt_le_sm_passkey_confirm(&key_cfm_param);
		if (RTK_BT_OK == ret) {
			BT_LOGA("[APP] Auth passkey auto confirm success\r\n");
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_AUTH_OOB_KEY_INPUT_IND: {
		rtk_bt_le_auth_oob_input_ind_t *oob_input_ind =
			(rtk_bt_le_auth_oob_input_ind_t *)param;
		BT_LOGA("[APP] Bond use oob key, conn_handle: %d. Please input the oob tk \r\n",
				oob_input_ind->conn_handle);
		break;
	}

	case RTK_BT_LE_GAP_EVT_AUTH_COMPLETE_IND: {
		rtk_bt_le_auth_complete_ind_t *auth_cplt_ind =
			(rtk_bt_le_auth_complete_ind_t *)param;
		if (auth_cplt_ind->err) {
			BT_LOGE("[APP] Pairing failed(err: 0x%x), conn_handle: %d\r\n",
					auth_cplt_ind->err, auth_cplt_ind->conn_handle);
		} else {
			BT_LOGA("[APP] Pairing success, conn_handle: %d\r\n", auth_cplt_ind->conn_handle);
			BT_DUMPHEXA("[APP] long term key is 0x", auth_cplt_ind->dev_ltk, auth_cplt_ind->dev_ltk_length, true);
		}
		break;
	}

	case RTK_BT_LE_GAP_EVT_BOND_MODIFY_IND: {
		rtk_bt_le_bond_modify_ind_t *bond_mdf_ind =
			(rtk_bt_le_bond_modify_ind_t *)param;
		char ident_addr[30] = {0};
		rtk_bt_le_addr_to_str(&(bond_mdf_ind->remote_addr), le_addr, sizeof(le_addr));
		rtk_bt_le_addr_to_str(&(bond_mdf_ind->ident_addr), ident_addr, sizeof(ident_addr));
		BT_LOGA("[APP] Bond info modified, op: %d, addr: %s, ident_addr: %s\r\n",
				bond_mdf_ind->op, le_addr, ident_addr);
		break;
	}

	default:
		BT_LOGE("[APP] Unknown gap cb evt type: %d\r\n", evt_code);
		break;
	}

	return RTK_BT_EVT_CB_OK;
}

static uint16_t app_get_gatts_app_id(uint8_t event, void *data)
{
	uint16_t app_id = 0xFFFF;

	switch (event) {
	case RTK_BT_GATTS_EVT_REGISTER_SERVICE: {
		rtk_bt_gatts_reg_ind_t *p_reg_ind = (rtk_bt_gatts_reg_ind_t *)data;
		app_id = p_reg_ind->app_id;
		break;
	}
	case RTK_BT_GATTS_EVT_READ_IND: {
		rtk_bt_gatts_read_ind_t *p_read_ind = (rtk_bt_gatts_read_ind_t *)data;
		app_id = p_read_ind->app_id;
		break;
	}
	case RTK_BT_GATTS_EVT_WRITE_IND: {
		rtk_bt_gatts_write_ind_t *p_write_ind = (rtk_bt_gatts_write_ind_t *)data;
		app_id = p_write_ind->app_id;
		break;
	}
	case RTK_BT_GATTS_EVT_CCCD_IND: {
		rtk_bt_gatts_cccd_ind_t *p_cccd_ind = (rtk_bt_gatts_cccd_ind_t *)data;
		app_id = p_cccd_ind->app_id;
		break;
	}
	case RTK_BT_GATTS_EVT_NOTIFY_COMPLETE_IND:
	case RTK_BT_GATTS_EVT_INDICATE_COMPLETE_IND: {
		rtk_bt_gatts_ntf_and_ind_ind_t *p_ind_ntf = (rtk_bt_gatts_ntf_and_ind_ind_t *)data;
		app_id = p_ind_ntf->app_id;
		break;
	}
	default:
		break;
	}

	return app_id;
}

static rtk_bt_evt_cb_ret_t ble_peripheral_gatts_app_callback(uint8_t event, void *data, uint32_t len)
{
	(void)len;
	uint16_t app_id = 0xFFFF;

	if (RTK_BT_GATTS_EVT_MTU_EXCHANGE == event) {
		rtk_bt_gatt_mtu_exchange_ind_t *p_gatt_mtu_ind = (rtk_bt_gatt_mtu_exchange_ind_t *)data;
		if (p_gatt_mtu_ind->result == RTK_BT_OK) {
			BT_LOGA("[APP] GATTS mtu exchange successfully, mtu_size: %d, conn_handle: %d \r\n",
					p_gatt_mtu_ind->mtu_size, p_gatt_mtu_ind->conn_handle);
		} else {
			BT_LOGE("[APP] GATTS mtu exchange fail \r\n");
		}
		return RTK_BT_EVT_CB_OK;
	} else if (RTK_BT_GATTS_EVT_CLIENT_SUPPORTED_FEATURES == event) {
		rtk_bt_gatts_client_supported_features_ind_t *p_ind = (rtk_bt_gatts_client_supported_features_ind_t *)data;
		if (p_ind->features & RTK_BT_GATTS_CLIENT_SUPPORTED_FEATURES_EATT_BEARER_BIT) {
			BT_LOGA("[APP] Client Supported features is writed: conn_handle %d, features 0x%02x. Remote client supports EATT.\r\n",
					p_ind->conn_handle, p_ind->features);
		}
		return RTK_BT_EVT_CB_OK;
	} else if (RTK_BT_GATTS_EVT_SERVICE_CHANGED_CCCD_IND == event) {
		rtk_bt_gatts_service_changed_cccd_ind_t *srv_change = (rtk_bt_gatts_service_changed_cccd_ind_t *)data;
		BT_LOGA("[APP] Service Changed cccd is updated, conn_handle: %d, cccd_enable: %d\r\n",
				srv_change->conn_handle, srv_change->cccd_enable);
		return RTK_BT_EVT_CB_OK;
	}

	app_id = app_get_gatts_app_id(event, data);
	switch (app_id) {
	case SIMPLE_BLE_SRV_ID:
		simple_ble_service_callback(event, data);
		break;
	case DEVICE_INFO_SRV_ID:
		device_info_srv_callback(event, data);
		break;
	case HEART_RATE_SRV_ID:
		heart_rate_srv_callback(event, data);
		break;
	case BATTERY_SRV_ID:
		battery_service_callback(event, data);
		break;
	case IMMEDIATE_ALERT_SRV_ID:
		immediate_alert_srv_callback(event, data);
		break;
	case GLUCOSE_SRV_ID:
		glucose_srv_callback(event, data);
		break;
	case DASHBOARD_HID_CC_SRV_ID:
		dashboard_hid_cc_srv_callback(event, data);
		break;
	case LONG_UUID_SRV_ID:
		long_uuid_service_callback(event, data);
		break;

	default:
		break;
	}

	return RTK_BT_EVT_CB_OK;
}

static int ble_peripheral_init(void)
{
	rtk_bt_app_conf_t bt_app_conf = {0};
	rtk_bt_le_addr_t bd_addr = {(rtk_bt_le_addr_type_t)0, {0}};
	char addr_str[30] = {0};
	char name[30] = {0};
	rtk_bt_le_adv_param_t adv_param = {0};

	bt_app_conf.app_profile_support = RTK_BT_PROFILE_GATTS;
	bt_app_conf.mtu_size = 180;
	bt_app_conf.master_init_mtu_req = true;
	bt_app_conf.slave_init_mtu_req = false;
	bt_app_conf.prefer_all_phy = RTK_BT_LE_PHYS_PREFER_ALL;
	bt_app_conf.prefer_tx_phy = RTK_BT_LE_PHYS_PREFER_1M | RTK_BT_LE_PHYS_PREFER_2M | RTK_BT_LE_PHYS_PREFER_CODED;
	bt_app_conf.prefer_rx_phy = RTK_BT_LE_PHYS_PREFER_1M | RTK_BT_LE_PHYS_PREFER_2M | RTK_BT_LE_PHYS_PREFER_CODED;
	bt_app_conf.max_tx_octets = 0x40;
	bt_app_conf.max_tx_time = 0x200;
	bt_app_conf.user_def_service = false;
	bt_app_conf.cccd_not_check = false;

	BT_APP_PROCESS(rtk_bt_enable(&bt_app_conf));

	//rtk_bt_le_sm_clear_bond_list();

	BT_APP_PROCESS(rtk_bt_le_gap_get_bd_addr(&bd_addr));
	rtk_bt_le_addr_to_str(&bd_addr, addr_str, sizeof(addr_str));
	BT_LOGA("[APP] BD_ADDR: %s\r\n", addr_str);

	BT_APP_PROCESS(rtk_bt_evt_register_callback(RTK_BT_COMMON_GP_GAP, peripheral_gap_app_callback));
	BT_APP_PROCESS(rtk_bt_evt_register_callback(RTK_BT_LE_GP_GAP, ble_peripheral_gap_app_callback));
	memcpy(name, (const char *)RTK_BT_DEV_NAME, strlen((const char *)RTK_BT_DEV_NAME));
	BT_APP_PROCESS(rtk_bt_le_gap_set_device_name((uint8_t *)name));
	BT_APP_PROCESS(rtk_bt_le_gap_set_appearance(RTK_BT_LE_GAP_APPEARANCE_GENERIC_HID));

	BT_APP_PROCESS(rtk_bt_le_sm_set_security_param(&sec_param));

	memcpy(&adv_param, &def_adv_param, sizeof(rtk_bt_le_adv_param_t));

	BT_APP_PROCESS(rtk_bt_evt_register_callback(RTK_BT_LE_GP_GATTS, ble_peripheral_gatts_app_callback));
	BT_APP_PROCESS(dashboard_hid_cc_srv_add());
	BT_APP_PROCESS(rtk_bt_le_gap_set_adv_data(adv_data, sizeof(adv_data)));
	BT_APP_PROCESS(rtk_bt_le_gap_set_scan_rsp_data(scan_rsp_data, sizeof(scan_rsp_data)));
	BT_APP_PROCESS(rtk_bt_le_gap_start_adv(&adv_param));

	return 0;
}

static void ble_peripheral_deinit(void)
{
	uint16_t ret = rtk_bt_disable();
	if (ret != RTK_BT_OK) {
		BT_LOGE("[APP] rtk_bt_disable failed! err: 0x%x\r\n", ret);
	}
	app_server_deinit();
}

/* Key send hook (registered with io_peripheral). Runs in the producer context
 * (button IRQ / CLI): just enqueues the event; the actual HID notify happens in
 * dash_board_ble_task where it is safe to call the rtk_bt APIs. */
static void ble_key_send_hook(key_event_t event)
{
	if (g_ble_key_queue) {
		key_msg_t msg = { .event = event };
		rtos_queue_send(g_ble_key_queue, &msg, 0);
	}
}

void dash_board_ble_task(void *param)
{
	UNUSED(param);

	RTK_LOGI(TAG, "ble_task: waiting for WiFi init...\n");

#if defined(CONFIG_WLAN) && CONFIG_WLAN
	while (!(wifi_is_running(STA_WLAN_INDEX) || wifi_is_running(SOFTAP_WLAN_INDEX))) {
		rtos_time_delay_ms(500);
	}
	RTK_LOGI(TAG, "ble_task: WiFi ready, starting BLE...\n");
#endif

	if (rtos_queue_create(&g_ble_key_queue, 16, sizeof(key_msg_t)) != RTK_SUCCESS) {
		RTK_LOGE(TAG, "Failed to create key queue\n");
		rtos_task_delete(NULL);
		return;
	}
	key_set_send_hook(ble_key_send_hook);

	if (ble_peripheral_init() == 0) {
		RTK_LOGI(TAG, "BLE initialized successfully\n");
		while (1) {
			key_msg_t msg;
			if (rtos_queue_receive(g_ble_key_queue, &msg, 0xFFFFFFFF) == RTK_SUCCESS) {
				switch (msg.event) {
				case KEY_NEXT_TRACK_PRESS:
					dashboard_hid_cc_next_track(g_ble_conn_handle, true);
					break;
				case KEY_NEXT_TRACK_RELEASE:
					dashboard_hid_cc_next_track(g_ble_conn_handle, false);
					break;
				case KEY_PREV_TRACK_PRESS:
					dashboard_hid_cc_prev_track(g_ble_conn_handle, true);
					break;
				case KEY_PREV_TRACK_RELEASE:
					dashboard_hid_cc_prev_track(g_ble_conn_handle, false);
					break;
				case KEY_PLAY_PAUSE_PRESS:
					dashboard_hid_cc_play_pause(g_ble_conn_handle, true);
					break;
				case KEY_PLAY_PAUSE_RELEASE:
					dashboard_hid_cc_play_pause(g_ble_conn_handle, false);
					break;
				default:
					break;
				}
			}
		}
	}

	ble_peripheral_deinit();

	RTK_LOGI(TAG, "ble_task end\n");

	rtos_task_delete(NULL);
}
