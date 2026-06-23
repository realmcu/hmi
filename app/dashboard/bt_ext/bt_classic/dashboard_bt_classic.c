/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * BT Classic (BR/EDR) core: GAP/SSP config and event dispatch for the A2DP Sink + AVRCP audio device.
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>

#include "ameba_soc.h"

#include "bte.h"
#include "gap.h"
#include "gap_br.h"
#include "btm.h"
#include "bt_bond.h"

#include "dashboard_bt_classic.h"
#include "bt_classic_config.h"
#include "bt_classic_link.h"
#include "bt_classic_a2dp.h"
#include "bt_classic_avrcp.h"
#include "bt_classic_hfp.h"
#include "bt_classic_sdp.h"
#include "bt_classic_spp.h"
#include "bt_classic_hid.h"

static const char *const TAG = "BTEXT_CLAS";

/* Timing params are in BT slots (1 slot = 0.625ms). */

#define BT_CLASSIC_DEVICE_NAME              "AMEBA_BT_AUDIO"

/* Link supervision timeout: 0x1f40 = 8000 slots = 5s. */
#define BT_CLASSIC_SUPVISION_TIMEOUT        0x1f40

#define BT_CLASSIC_PAGESCAN_WINDOW          0x48
#define BT_CLASSIC_PAGESCAN_INTERVAL        0x800
#define BT_CLASSIC_PAGE_TIMEOUT             0x2000
#define BT_CLASSIC_INQUIRYSCAN_WINDOW       0x48
#define BT_CLASSIC_INQUIRYSCAN_INTERVAL     0x800

static const char *bt_classic_cause_str(uint16_t cause)
{
	switch (cause & 0xFF) {
	case HCI_ERR_PAGE_TIMEOUT:          return "PAGE_TIMEOUT";
	case HCI_ERR_AUTHEN_FAIL:           return "AUTHEN_FAIL";
	case HCI_ERR_KEY_MISSING:           return "KEY_MISSING(peer deleted bond)";
	case HCI_ERR_CONN_TIMEOUT:          return "CONN_TIMEOUT(link supervision)";
	case HCI_ERR_REMOTE_USER_TERMINATE: return "REMOTE_USER_TERMINATE(peer closed)";
	case HCI_ERR_REMOTE_LOW_RESOURCE:   return "REMOTE_LOW_RESOURCE";
	case HCI_ERR_REMOTE_POWER_OFF:      return "REMOTE_POWER_OFF";
	case HCI_ERR_LOCAL_HOST_TERMINATE:  return "LOCAL_HOST_TERMINATE(we/stack closed it)";
	case HCI_ERR_REPEATED_ATTEMPTS:     return "REPEATED_ATTEMPTS";
	case HCI_ERR_PARING_NOT_ALLOWED:    return "PAIRING_NOT_ALLOWED";
	default:                            return "other";
	}
}

/* Must run after bt_mgr_init() and before gap_start_bt_stack(). */
static void bt_classic_gap_config(void)
{
	uint8_t  device_name[GAP_DEVICE_NAME_LEN] = BT_CLASSIC_DEVICE_NAME;
	uint32_t class_of_device   = BT_CLASSIC_CLASS_OF_DEVICE;
	uint16_t supervision_tout  = BT_CLASSIC_SUPVISION_TIMEOUT;

	uint16_t link_policy       = GAP_LINK_POLICY_ROLE_SWITCH | GAP_LINK_POLICY_SNIFF_MODE;

	uint8_t  radio_mode           = GAP_RADIO_MODE_VISIBLE_CONNECTABLE;
	bool     limited_discoverable = false;
	bool     auto_accept_acl      = true;

	uint8_t  pagescan_type      = GAP_PAGE_SCAN_TYPE_INTERLACED;
	uint16_t pagescan_interval  = BT_CLASSIC_PAGESCAN_INTERVAL;
	uint16_t pagescan_window    = BT_CLASSIC_PAGESCAN_WINDOW;
	uint16_t page_timeout       = BT_CLASSIC_PAGE_TIMEOUT;

	uint8_t  inquiryscan_type     = GAP_INQUIRY_SCAN_TYPE_INTERLACED;
	uint16_t inquiryscan_window   = BT_CLASSIC_INQUIRYSCAN_WINDOW;
	uint16_t inquiryscan_interval = BT_CLASSIC_INQUIRYSCAN_INTERVAL;
	uint8_t  inquiry_mode         = GAP_INQUIRY_MODE_EXTENDED_RESULT;

	/* SSP params: NO_INPUT_NO_OUTPUT IO cap selects Just Works pairing. */
	uint8_t  pair_mode  = GAP_PAIRING_MODE_PAIRABLE;
	uint16_t auth_flags = GAP_AUTHEN_BIT_GENERAL_BONDING_FLAG | GAP_AUTHEN_BIT_SC_FLAG;
	uint8_t  io_cap     = GAP_IO_CAP_NO_INPUT_NO_OUTPUT;
	uint8_t  oob_enable = false;
	uint8_t  bt_mode    = GAP_BT_MODE_21ENABLED;

	gap_br_set_param(GAP_BR_PARAM_NAME, GAP_DEVICE_NAME_LEN, device_name);

	gap_set_param(GAP_PARAM_BOND_PAIRING_MODE, sizeof(uint8_t), &pair_mode);
	gap_set_param(GAP_PARAM_BOND_AUTHEN_REQUIREMENTS_FLAGS, sizeof(uint16_t), &auth_flags);
	gap_set_param(GAP_PARAM_BOND_IO_CAPABILITIES, sizeof(uint8_t), &io_cap);
	gap_set_param(GAP_PARAM_BOND_OOB_ENABLED, sizeof(uint8_t), &oob_enable);

	gap_br_set_param(GAP_BR_PARAM_BT_MODE, sizeof(uint8_t), &bt_mode);
	gap_br_set_param(GAP_BR_PARAM_COD, sizeof(uint32_t), &class_of_device);
	gap_br_set_param(GAP_BR_PARAM_LINK_POLICY, sizeof(uint16_t), &link_policy);
	gap_br_set_param(GAP_BR_PARAM_SUPV_TOUT, sizeof(uint16_t), &supervision_tout);
	gap_br_set_param(GAP_BR_PARAM_AUTO_ACCEPT_ACL, sizeof(bool), &auto_accept_acl);

	gap_br_set_param(GAP_BR_PARAM_RADIO_MODE, sizeof(uint8_t), &radio_mode);
	gap_br_set_param(GAP_BR_PARAM_LIMIT_DISCOV, sizeof(bool), &limited_discoverable);

	gap_br_set_param(GAP_BR_PARAM_PAGE_SCAN_TYPE, sizeof(uint8_t), &pagescan_type);
	gap_br_set_param(GAP_BR_PARAM_PAGE_SCAN_INTERVAL, sizeof(uint16_t), &pagescan_interval);
	gap_br_set_param(GAP_BR_PARAM_PAGE_SCAN_WINDOW, sizeof(uint16_t), &pagescan_window);
	gap_br_set_param(GAP_BR_PARAM_PAGE_TIMEOUT, sizeof(uint16_t), &page_timeout);

	gap_br_set_param(GAP_BR_PARAM_INQUIRY_SCAN_TYPE, sizeof(uint8_t), &inquiryscan_type);
	gap_br_set_param(GAP_BR_PARAM_INQUIRY_SCAN_INTERVAL, sizeof(uint16_t), &inquiryscan_interval);
	gap_br_set_param(GAP_BR_PARAM_INQUIRY_SCAN_WINDOW, sizeof(uint16_t), &inquiryscan_window);
	gap_br_set_param(GAP_BR_PARAM_INQUIRY_MODE, sizeof(uint8_t), &inquiry_mode);
}

static void app_gap_common_callback(uint8_t cb_type, void *p_cb_data)
{
	(void)p_cb_data;
	RTK_LOGS(TAG, RTK_LOG_DEBUG, "gap common cb_type %d\r\n", cb_type);
}

static void bt_classic_gap_handle_event(T_BT_EVENT event_type, void *event_buf,
										uint16_t buf_len)
{
	T_BT_EVENT_PARAM    *param = event_buf;
	T_BT_CLASSIC_BR_LINK *p_link;

	(void)buf_len;

	switch (event_type) {
	case BT_EVENT_READY:
		memcpy(bt_classic_db.local_addr, param->ready.bd_addr, 6);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> BT STACK READY, local addr " BD_FMT " <<<\r\n",
			BD_ARG(bt_classic_db.local_addr));
		break;

	case BT_EVENT_ACL_CONN_SUCCESS:
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> ACL CONNECTED " BD_FMT " <<<\r\n",
			BD_ARG(param->acl_conn_success.bd_addr));
		break;

	case BT_EVENT_LINK_USER_CONFIRMATION_REQ:
		/* Just Works: accept without user interaction. */
		gap_br_user_cfm_req_cfm(param->link_user_confirmation_req.bd_addr,
								GAP_CFM_CAUSE_ACCEPT);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"user confirmation req " BD_FMT ", accept\r\n",
			BD_ARG(param->link_user_confirmation_req.bd_addr));
		break;

	case BT_EVENT_ACL_AUTHEN_SUCCESS:
		if (bt_classic_alloc_br_link(param->acl_authen_success.bd_addr) != NULL) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> AUTHENTICATED " BD_FMT " (link alloc) <<<\r\n",
				BD_ARG(param->acl_authen_success.bd_addr));
		} else {
			RTK_LOGS(TAG, RTK_LOG_ERROR,
				"AUTHENTICATED but link table full " BD_FMT "\r\n",
				BD_ARG(param->acl_authen_success.bd_addr));
		}
		break;

	case BT_EVENT_ACL_CONN_ENCRYPTED:
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> LINK ENCRYPTED " BD_FMT " <<<\r\n",
			BD_ARG(param->acl_conn_encrypted.bd_addr));
		break;

	case BT_EVENT_ACL_CONN_DISCONN:
		p_link = bt_classic_find_br_link(param->acl_conn_disconn.bd_addr);
		bt_classic_free_br_link(p_link);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> ACL DISCONNECTED " BD_FMT ", cause 0x%04x (%s) <<<\r\n",
			BD_ARG(param->acl_conn_disconn.bd_addr),
			param->acl_conn_disconn.cause,
			bt_classic_cause_str(param->acl_conn_disconn.cause));
		break;

	case BT_EVENT_LINK_KEY_INFO:
		bt_bond_key_set(param->link_key_info.bd_addr,
						param->link_key_info.link_key,
						param->link_key_info.key_type);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"link key saved for " BD_FMT " (type %d)\r\n",
			BD_ARG(param->link_key_info.bd_addr),
			param->link_key_info.key_type);
		break;

	case BT_EVENT_LINK_KEY_REQ: {
		uint8_t            link_key[16];
		T_BT_LINK_KEY_TYPE type = BT_LINK_KEY_TYPE_COMBINATION;

		if (bt_bond_key_get(param->link_key_req.bd_addr, link_key,
							(uint8_t *)&type)) {
			bt_link_key_cfm(param->link_key_req.bd_addr, true, type, link_key);
			RTK_LOGS(TAG, RTK_LOG_INFO, "link key req " BD_FMT ": found\r\n",
					 BD_ARG(param->link_key_req.bd_addr));
		} else {
			bt_link_key_cfm(param->link_key_req.bd_addr, false, type, link_key);
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"link key req " BD_FMT ": not found, will pair\r\n",
				BD_ARG(param->link_key_req.bd_addr));
		}
		break;
	}

	case BT_EVENT_LINK_PIN_CODE_REQ: {
		/* Legacy (pre-2.1) PIN pairing fallback: reply default "0000". */
		uint8_t pin_code[4] = {'0', '0', '0', '0'};

		bt_link_pin_code_cfm(param->link_pin_code_req.bd_addr, pin_code,
							 sizeof(pin_code), true);
		RTK_LOGS(TAG, RTK_LOG_INFO, "pin code req " BD_FMT ", reply default\r\n",
				 BD_ARG(param->link_pin_code_req.bd_addr));
		break;
	}

	default:
		/* Must exclude high-rate SCO_DATA_IND: logging it floods UART and stalls the BT event thread. */
		if (event_type == BT_EVENT_SCO_DATA_IND) {
		} else if (event_type < 0x3000) {
			RTK_LOGS(TAG, RTK_LOG_INFO, "gap/other event 0x%04x\r\n", event_type);
		} else {
			RTK_LOGS(TAG, RTK_LOG_DEBUG, "gap event 0x%04x\r\n", event_type);
		}
		break;
	}
}

int dashboard_bt_classic_init(void)
{
	RTK_LOGS(TAG, RTK_LOG_INFO, "BT Classic (A2DP sink + AVRCP) init start\r\n");

	/* bt_mgr_init() must run before GAP config. */
	if (!bt_mgr_init()) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "bt_mgr_init failed\r\n");
		return -1;
	}

	bt_classic_gap_config();

	gap_register_app_cb(app_gap_common_callback);

	bt_classic_link_db_reset();

	RTK_LOGS(TAG, RTK_LOG_INFO,
		"BT Classic init done (discoverable + connectable)\r\n");
	return 0;
}

void dashboard_bt_classic_btmgr_callback(T_BT_EVENT event_type, void *event_buf,
										 uint16_t buf_len)
{
	/* Single callback dispatcher: each handler only acts on its own non-overlapping event range. */
	bt_classic_gap_handle_event(event_type, event_buf, buf_len);
#if BT_CLASSIC_ENABLE_A2DP
	bt_classic_a2dp_handle_event(event_type, event_buf, buf_len);
#endif
#if BT_CLASSIC_ENABLE_AVRCP
	bt_classic_avrcp_handle_event(event_type, event_buf, buf_len);
#endif
#if BT_CLASSIC_ENABLE_HFP
	bt_classic_hfp_handle_event(event_type, event_buf, buf_len);
#endif
#if BT_CLASSIC_ENABLE_HID
	bt_classic_hid_handle_event(event_type, event_buf, buf_len);
#endif
#if BT_CLASSIC_ENABLE_SPP
	bt_classic_spp_handle_event(event_type, event_buf, buf_len);
#endif
}

int dashboard_bt_classic_profile_init(void)
{
	/* Call sdp_init() last; it registers SDP records for the enabled profiles. */
#if BT_CLASSIC_ENABLE_AVRCP
	bt_classic_avrcp_init();
#endif
#if BT_CLASSIC_ENABLE_A2DP
	bt_classic_a2dp_init();
#endif
#if BT_CLASSIC_ENABLE_HFP
	bt_classic_hfp_init();
#endif
#if BT_CLASSIC_ENABLE_HID
	bt_classic_hid_init();
#endif
#if BT_CLASSIC_ENABLE_SPP
	bt_classic_spp_init();
#endif
	bt_classic_sdp_init();

	RTK_LOGS(TAG, RTK_LOG_INFO,
		"BT Classic profile init done (A2DP=%d AVRCP=%d HFP=%d HID=%d SPP=%d)\r\n",
		BT_CLASSIC_ENABLE_A2DP, BT_CLASSIC_ENABLE_AVRCP, BT_CLASSIC_ENABLE_HFP,
		BT_CLASSIC_ENABLE_HID, BT_CLASSIC_ENABLE_SPP);
	return 0;
}

void dashboard_bt_classic_key_handler(key_event_t event)
{
#if BT_CLASSIC_ENABLE_HID
	bt_classic_hid_key_handler(event);
#else
	(void)event;
#endif
}

#endif /* CONFIG_BT_EXT */
