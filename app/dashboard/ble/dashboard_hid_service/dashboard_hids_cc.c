#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <osif.h>

#include <bt_api_config.h>
#include <rtk_bt_def.h>
#include <rtk_bt_common.h>
#include <rtk_bt_le_gap.h>
#include <rtk_bt_att_defs.h>
#include <rtk_bt_gatts.h>
#include <bt_utils.h>

#include "dashboard_hids_cc.h"

#define HID_SRV_UUID                        0x1812
#define PROTOCOL_MODE_CHAR_UUID              0x2A4E
#define REPORT_CHAR_UUID                    0x2A4D
#define REPORT_MAP_CHAR_UUID                0x2A4B
#define HID_INFO_CHAR_UUID                  0x2A4A
#define HID_CONTROL_POINT_CHAR_UUID         0x2A4C
#define EXTERNAL_REPORT_REFERENCE_DSCRP_UUID 0x2907
#define REPORT_REFERENCE_DSCRP_UUID         0x2908

#define RTK_BT_UUID_HID_SRV                 BT_UUID_DECLARE_16(HID_SRV_UUID)
#define RTK_BT_UUID_PROTOCOL_MODE_CHAR      BT_UUID_DECLARE_16(PROTOCOL_MODE_CHAR_UUID)
#define RTK_BT_UUID_REPORT_CHAR             BT_UUID_DECLARE_16(REPORT_CHAR_UUID)
#define RTK_BT_UUID_REPORT_MAP_CHAR         BT_UUID_DECLARE_16(REPORT_MAP_CHAR_UUID)
#define RTK_BT_UUID_HID_INFO_CHAR           BT_UUID_DECLARE_16(HID_INFO_CHAR_UUID)
#define RTK_BT_UUID_HID_CONTROL_POINT_CHAR  BT_UUID_DECLARE_16(HID_CONTROL_POINT_CHAR_UUID)
#define RTK_BT_UUID_EXT_REPORT_REF_DSCRP    BT_UUID_DECLARE_16(EXTERNAL_REPORT_REFERENCE_DSCRP_UUID)
#define RTK_BT_UUID_REPORT_REFERENCE_DSCRP  BT_UUID_DECLARE_16(REPORT_REFERENCE_DSCRP_UUID)

#define HID_CC_REPORT_ID                    0x01

typedef struct {
	uint16_t bcd_hid;
	uint8_t  b_country_code;
	uint8_t  flags;
} __attribute__((packed)) T_HID_INFO_LOCAL;

#define HID_INPUT_TYPE_LOCAL  1

#define PROTOCOL_MODE_CHAR_VAL_INDEX        2
#define REPORT_MAP_CHAR_VAL_INDEX           4
#define EXT_REPORT_REF_DSCRP_INDEX          5
#define REPORT_INPUT_CHAR_VAL_INDEX         7
#define REPORT_INPUT_CHAR_CCCD_INDEX        8
#define REPORT_REF_DSCRP_INDEX              9
#define HID_INFO_CHAR_VAL_INDEX             11
#define HID_CONTROL_POINT_CHAR_VAL_INDEX    13

static uint8_t g_hid_cc_cccd_ntf_en_map[RTK_BLE_GAP_MAX_LINKS] = {0};

static T_HID_INFO_LOCAL g_hid_cc_info = {0x0100, 0, 0x02};

static uint16_t g_hid_input_cc_report = HID_INPUT_TYPE_LOCAL << 8 | HID_CC_REPORT_ID;

static const uint8_t g_hid_cc_report_descriptor[] = {
	0x05, 0x0c,        /* USAGE_PAGE (Consumer) */
	0x09, 0x01,        /* USAGE (Consumer Control) */
	0xa1, 0x01,        /* COLLECTION (Application) */
	0x85, HID_CC_REPORT_ID,  /* REPORT_ID (1) */
	0x15, 0x00,        /* LOGICAL_MINIMUM (0) */
	0x25, 0x01,        /* LOGICAL_MAXIMUM (1) */
	0x75, 0x01,        /* REPORT_SIZE (1) */
	0x95, 0x08,        /* REPORT_COUNT (8) */
	0x09, 0xB5,        /* USAGE (Scan Next Track) */
	0x09, 0xB6,        /* USAGE (Scan Previous Track) */
	0x09, 0xB7,        /* USAGE (Stop) */
	0x09, 0xCD,        /* USAGE (Play/Pause) */
	0x09, 0xE2,        /* USAGE (Mute) */
	0x09, 0xE9,        /* USAGE (Volume Up) */
	0x09, 0xEA,        /* USAGE (Volume Down) */
	0x0a, 0x83, 0x01,  /* USAGE (AL Consumer Control Config) */
	0x81, 0x02,        /* INPUT (Data,Var,Abs) */
	0xc0,              /* END_COLLECTION */
};

static uint8_t g_hid_protocol_mode = 0x01;

static rtk_bt_gatt_attr_t g_hid_cc_srv_attrs[] = {
	/* 0 */
	RTK_BT_GATT_PRIMARY_SERVICE(RTK_BT_UUID_HID_SRV),

	/* 1, 2 Protocol Mode */
	RTK_BT_GATT_CHARACTERISTIC(RTK_BT_UUID_PROTOCOL_MODE_CHAR,
							   RTK_BT_GATT_CHRC_READ | RTK_BT_GATT_CHRC_WRITE_WITHOUT_RESP,
							   RTK_BT_GATT_PERM_READ_AUTHEN | RTK_BT_GATT_PERM_WRITE_AUTHEN),

	/* 3, 4 Report Map */
	RTK_BT_GATT_CHARACTERISTIC(RTK_BT_UUID_REPORT_MAP_CHAR,
							   RTK_BT_GATT_CHRC_READ,
							   RTK_BT_GATT_PERM_READ_AUTHEN),
	/* 5 */
	RTK_BT_GATT_DESCRIPTOR(RTK_BT_UUID_EXT_REPORT_REF_DSCRP,
						   RTK_BT_GATT_PERM_READ_AUTHEN, NULL, 0, RTK_BT_GATT_APP),

	/* 6, 7 Input Report */
	RTK_BT_GATT_CHARACTERISTIC(RTK_BT_UUID_REPORT_CHAR,
							   RTK_BT_GATT_CHRC_READ | RTK_BT_GATT_CHRC_WRITE | RTK_BT_GATT_CHRC_NOTIFY,
							   RTK_BT_GATT_PERM_READ_AUTHEN | RTK_BT_GATT_PERM_WRITE_AUTHEN),
	/* 8 */
	RTK_BT_GATT_CCC(RTK_BT_GATT_PERM_READ | RTK_BT_GATT_PERM_WRITE),
	/* 9 */
	RTK_BT_GATT_DESCRIPTOR(RTK_BT_UUID_REPORT_REFERENCE_DSCRP,
						   RTK_BT_GATT_PERM_READ_AUTHEN, &g_hid_input_cc_report, 2, RTK_BT_GATT_INTERNAL),

	/* 10, 11 HID Info */
	RTK_BT_GATT_CHARACTERISTIC(RTK_BT_UUID_HID_INFO_CHAR,
							   RTK_BT_GATT_CHRC_READ,
							   RTK_BT_GATT_PERM_READ_AUTHEN),

	/* 12, 13 Control Point */
	RTK_BT_GATT_CHARACTERISTIC(RTK_BT_UUID_HID_CONTROL_POINT_CHAR,
							   RTK_BT_GATT_CHRC_WRITE_WITHOUT_RESP,
							   RTK_BT_GATT_PERM_WRITE_AUTHEN),
};

static struct rtk_bt_gatt_service g_hid_cc_srv = RTK_BT_GATT_SERVICE(g_hid_cc_srv_attrs, DASHBOARD_HID_CC_SRV_ID);

static void hid_cc_read_hdl(void *data)
{
	uint16_t ret = 0;
	rtk_bt_gatts_read_ind_t *p_read_ind = (rtk_bt_gatts_read_ind_t *)data;
	rtk_bt_gatts_read_resp_param_t read_resp = {0};
	uint16_t offset = p_read_ind->offset;
	read_resp.app_id = p_read_ind->app_id;
	read_resp.conn_handle = p_read_ind->conn_handle;
	read_resp.cid = p_read_ind->cid;
	read_resp.index = p_read_ind->index;

	BT_LOGA("[APP] HID CC read event, conn_handle: %d, index: %d, offset: %d\r\n",
			read_resp.conn_handle, read_resp.index, offset);

	switch (p_read_ind->index) {
	case PROTOCOL_MODE_CHAR_VAL_INDEX:
		read_resp.data = &g_hid_protocol_mode;
		read_resp.len = sizeof(g_hid_protocol_mode);
		BT_LOGA("[APP] HID CC Protocol Mode read, mode: %d\r\n", g_hid_protocol_mode);
		break;
	case REPORT_MAP_CHAR_VAL_INDEX: {
		uint16_t total_len = sizeof(g_hid_cc_report_descriptor);
		uint16_t actual_len = total_len - offset;
		read_resp.data = (void *)((uint8_t *)&g_hid_cc_report_descriptor[0] + offset);
		read_resp.len = actual_len;
		BT_LOGA("[APP] HID CC Report Map read, total: %d, offset: %d, sending: %d\r\n",
				total_len, offset, actual_len);
		break;
	}
	case EXT_REPORT_REF_DSCRP_INDEX:
		BT_LOGA("[APP] HID CC External Report Ref read\r\n");
		read_resp.data = NULL;
		read_resp.len = 0;
		break;
	case REPORT_INPUT_CHAR_VAL_INDEX:
		BT_LOGA("[APP] HID CC Input Report read\r\n");
		read_resp.data = NULL;
		read_resp.len = 0;
		break;
	case HID_INFO_CHAR_VAL_INDEX:
		read_resp.data = (uint8_t *)&g_hid_cc_info;
		read_resp.len = sizeof(g_hid_cc_info);
		BT_LOGA("[APP] HID CC HID Info read, bcdHID: 0x%04x\r\n", g_hid_cc_info.bcd_hid);
		break;
	case HID_CONTROL_POINT_CHAR_VAL_INDEX:
		BT_LOGA("[APP] HID CC Control Point read, not allowed\r\n");
		read_resp.err_code = RTK_BT_ATT_ERR_ATTR_NOT_FOUND;
		break;
	default:
		BT_LOGE("[APP] HID CC read event unknown index: %d\r\n", p_read_ind->index);
		read_resp.err_code = RTK_BT_ATT_ERR_ATTR_NOT_FOUND;
		break;
	}

	ret = rtk_bt_gatts_read_resp(&read_resp);
	if (RTK_BT_OK == ret) {
		BT_LOGA("[APP] HID CC read resp success, index: %d\r\n", read_resp.index);
	} else {
		BT_LOGE("[APP] HID CC read resp failed, index: %d, err: 0x%x\r\n", read_resp.index, ret);
	}
}

static void hid_cc_write_hdl(void *data)
{
	uint16_t ret = 0;
	rtk_bt_gatts_write_ind_t *p_write_ind = (rtk_bt_gatts_write_ind_t *)data;
	rtk_bt_gatts_write_resp_param_t write_resp = {0};
	write_resp.app_id = p_write_ind->app_id;
	write_resp.conn_handle = p_write_ind->conn_handle;
	write_resp.cid = p_write_ind->cid;
	write_resp.index = p_write_ind->index;
	write_resp.type = p_write_ind->type;

	BT_LOGA("[APP] HID CC write event, conn_handle: %d, index: %d, len: %d, type: %d\r\n",
			write_resp.conn_handle, write_resp.index, p_write_ind->len, write_resp.type);

	if (p_write_ind->len > 0 && p_write_ind->value) {
		BT_DUMPA("[APP] HID CC write data: ", p_write_ind->value, p_write_ind->len);
	}

	if (RTK_BT_GATTS_WRITE_NO_RESP == p_write_ind->type ||
		RTK_BT_GATTS_WRITE_NO_RESP_SIGNED == p_write_ind->type) {
		BT_LOGA("[APP] HID CC write no response needed\r\n");
		return;
	}

	if (!p_write_ind->len || !p_write_ind->value) {
		BT_LOGE("[APP] HID CC write value is empty!\r\n");
		write_resp.err_code = RTK_BT_ATT_ERR_INVALID_VALUE_SIZE;
		goto send_write_rsp;
	}

	switch (p_write_ind->index) {
	case PROTOCOL_MODE_CHAR_VAL_INDEX:
		g_hid_protocol_mode = *(uint8_t *)p_write_ind->value;
		BT_LOGA("[APP] HID CC Protocol Mode write, value: %d\r\n", g_hid_protocol_mode);
		break;
	case HID_CONTROL_POINT_CHAR_VAL_INDEX:
		BT_LOGA("[APP] HID CC Control Point write, value: 0x%02x\r\n", *(uint8_t *)p_write_ind->value);
		break;
	default:
		BT_LOGE("[APP] HID CC write event unknown index: %d\r\n", p_write_ind->index);
		write_resp.err_code = RTK_BT_ATT_ERR_ATTR_NOT_FOUND;
		break;
	}

send_write_rsp:
	ret = rtk_bt_gatts_write_resp(&write_resp);
	if (RTK_BT_OK == ret) {
		BT_LOGA("[APP] HID CC write resp success, index: %d\r\n", write_resp.index);
	} else {
		BT_LOGE("[APP] HID CC write resp failed, index: %d, err: 0x%x\r\n", write_resp.index, ret);
	}
}

static void hid_cc_cccd_update_hdl(void *data)
{
	rtk_bt_gatts_cccd_ind_t *p_cccd_ind = (rtk_bt_gatts_cccd_ind_t *)data;
	uint16_t conn_handle = p_cccd_ind->conn_handle;
	uint8_t cccd_ntf = p_cccd_ind->value & RTK_BT_GATT_CCC_NOTIFY;
	uint8_t conn_id;

	BT_LOGA("[APP] HID CC CCCD event, conn_handle: %d, index: %d, value: 0x%04x\r\n",
			conn_handle, p_cccd_ind->index, p_cccd_ind->value);

	if (rtk_bt_le_gap_get_conn_id(conn_handle, &conn_id) != RTK_BT_OK) {
		BT_LOGE("[APP] HID CC CCCD get conn_id failed\r\n");
		return;
	}

	switch (p_cccd_ind->index) {
	case REPORT_INPUT_CHAR_CCCD_INDEX:
		if (cccd_ntf) {
			g_hid_cc_cccd_ntf_en_map[conn_id] = 1;
			BT_LOGA("[APP] HID CC notify CCCD enable, conn_id: %d\r\n", conn_id);
		} else {
			g_hid_cc_cccd_ntf_en_map[conn_id] = 0;
			BT_LOGA("[APP] HID CC notify CCCD disable, conn_id: %d\r\n", conn_id);
		}
		break;
	default:
		BT_LOGE("[APP] HID CC CCCD unknown index: %d\r\n", p_cccd_ind->index);
		break;
	}
}

void dashboard_hid_cc_srv_callback(uint8_t event, void *data)
{
	switch (event) {
	case RTK_BT_GATTS_EVT_REGISTER_SERVICE: {
		rtk_bt_gatts_reg_ind_t *reg_srv_res = (rtk_bt_gatts_reg_ind_t *)data;
		if (RTK_BT_OK == reg_srv_res->reg_status) {
			BT_LOGA("[APP] HID CC register service succeed! app_id: %d\r\n", reg_srv_res->app_id);
		} else {
			BT_LOGE("[APP] HID CC register service failed, err: 0x%x\r\n", reg_srv_res->reg_status);
		}
		break;
	}
	case RTK_BT_GATTS_EVT_READ_IND:
		hid_cc_read_hdl(data);
		break;
	case RTK_BT_GATTS_EVT_WRITE_IND:
		hid_cc_write_hdl(data);
		break;
	case RTK_BT_GATTS_EVT_CCCD_IND:
		hid_cc_cccd_update_hdl(data);
		break;
	case RTK_BT_GATTS_EVT_NOTIFY_COMPLETE_IND: {
		rtk_bt_gatts_ntf_and_ind_ind_t *p_ntf_ind = (rtk_bt_gatts_ntf_and_ind_ind_t *)data;
		if (RTK_BT_OK == p_ntf_ind->err_code) {
			BT_LOGA("[APP] HID CC notify succeed! conn_handle: %d, index: %d\r\n",
					p_ntf_ind->conn_handle, p_ntf_ind->index);
		} else {
			BT_LOGE("[APP] HID CC notify failed, conn_handle: %d, index: %d, err: 0x%x\r\n",
					p_ntf_ind->conn_handle, p_ntf_ind->index, p_ntf_ind->err_code);
		}
		break;
	}
	default:
		BT_LOGE("[APP] HID CC unknown event: %d\r\n", event);
		break;
	}
}

void dashboard_hid_cc_send_key(uint16_t conn_handle, uint16_t key_bitmap)
{
	uint8_t conn_id;

	if (rtk_bt_le_gap_get_conn_id(conn_handle, &conn_id) != RTK_BT_OK) {
		return;
	}

	if (!g_hid_cc_cccd_ntf_en_map[conn_id]) {
		return;
	}

	rtk_bt_gatts_ntf_and_ind_param_t ntf_param = {0};
	ntf_param.app_id = DASHBOARD_HID_CC_SRV_ID;
	ntf_param.conn_handle = conn_handle;
	ntf_param.index = REPORT_INPUT_CHAR_VAL_INDEX;
	ntf_param.data = &key_bitmap;
	ntf_param.len = sizeof(key_bitmap);

	rtk_bt_gatts_notify(&ntf_param);
}

void dashboard_hid_cc_prev_track(uint16_t conn_handle, bool press)
{
	printf("prev_track: %d, conn_handle: %d\n", press, conn_handle);
	dashboard_hid_cc_send_key(conn_handle, press ? (1 << DASHBOARD_HID_CC_SCAN_PREV_TRACK) : 0);
}

void dashboard_hid_cc_next_track(uint16_t conn_handle, bool press)
{
	printf("next_track: %d, conn_handle: %d\n", press, conn_handle);
	dashboard_hid_cc_send_key(conn_handle, press ? (1 << DASHBOARD_HID_CC_SCAN_NEXT_TRACK) : 0);
}

void dashboard_hid_cc_play_pause(uint16_t conn_handle, bool press)
{
	dashboard_hid_cc_send_key(conn_handle, press ? (1 << DASHBOARD_HID_CC_PLAY_PAUSE) : 0);
}

uint16_t dashboard_hid_cc_srv_add(void)
{
	g_hid_cc_srv.type = GATT_SERVICE_OVER_BLE;
	g_hid_cc_srv.server_info = 0;
	g_hid_cc_srv.user_data = NULL;
	g_hid_cc_srv.register_status = 0;

	return rtk_bt_gatts_register_service(&g_hid_cc_srv);
}

void dashboard_hid_cc_disconnect(uint16_t conn_handle)
{
	uint8_t conn_id;

	if (rtk_bt_le_gap_get_conn_id(conn_handle, &conn_id) != RTK_BT_OK) {
		return;
	}

	g_hid_cc_cccd_ntf_en_map[conn_id] = 0;
}

void dashboard_hid_cc_status_deinit(void)
{
	memset(g_hid_cc_cccd_ntf_en_map, 0, sizeof(g_hid_cc_cccd_ntf_en_map));
}
