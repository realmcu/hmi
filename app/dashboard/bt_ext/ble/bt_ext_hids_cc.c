/*
 * Copyright (c) 2025 Realtek Semiconductor Corporation. All rights reserved.
 *
 * BT EXT HID Consumer Control profile.
 *
 * Functional mirror of ble/dashboard_hid_service/dashboard_hids_cc.c, but built
 * on the bluetooth_ext (bee stack) profile-server framework instead of the
 * internal rtk_bt_gatts API. Same HID Consumer Control report map, same
 * characteristic layout and (MITM-authenticated) permissions.
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>
#include "ameba_soc.h"

#include "gap.h"
#include "profile_server.h"

#include "bt_ext_hids_cc.h"

static const char *const TAG = "BLEEXT_HID";

/* ---- HID over GATT UUIDs ------------------------------------------------- */
#define HID_SRV_UUID                 0x1812
#define PROTOCOL_MODE_CHAR_UUID       0x2A4E
#define REPORT_CHAR_UUID              0x2A4D
#define REPORT_MAP_CHAR_UUID          0x2A4B
#define HID_INFO_CHAR_UUID            0x2A4A
#define HID_CONTROL_POINT_CHAR_UUID   0x2A4C

/* ---- Flat attribute indices (must match bt_ext_hids_cc_tbl below) -------- */
#define BT_EXT_HID_PROTOCOL_MODE_VAL_INDEX   2
#define BT_EXT_HID_REPORT_MAP_VAL_INDEX      4
#define BT_EXT_HID_EXT_REPORT_REF_INDEX      5
#define BT_EXT_HID_REPORT_INPUT_VAL_INDEX    7
#define BT_EXT_HID_REPORT_INPUT_CCCD_INDEX   8
#define BT_EXT_HID_REPORT_REF_INDEX          9
#define BT_EXT_HID_INFO_VAL_INDEX            11
#define BT_EXT_HID_CONTROL_POINT_VAL_INDEX   13

#define HID_CC_REPORT_ID    0x01
#define HID_INPUT_TYPE      0x01   /* report reference: input report */

#define BT_EXT_HID_MAX_LINKS  4

typedef struct {
	uint16_t bcd_hid;
	uint8_t  b_country_code;
	uint8_t  flags;
} __attribute__((packed)) T_HID_INFO_LOCAL;

/* Consumer Control report map */
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

static uint8_t g_hid_protocol_mode = 0x01;   /* report protocol mode */
static T_HID_INFO_LOCAL g_hid_cc_info = {0x0100, 0, 0x02};
static uint8_t g_hid_cc_ntf_en[BT_EXT_HID_MAX_LINKS] = {0};

/* Service id assigned by server_add_service() */
static T_SERVER_ID g_hids_cc_service_id = 0xFF;

static P_FUN_SERVER_GENERAL_CB pfn_hids_cc_cb = NULL;

/* ---- GATT attribute table ------------------------------------------------ */
static const T_ATTRIB_APPL bt_ext_hids_cc_tbl[] = {
	/* 0: <<Primary Service>> HID */
	{
		(ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_LE),
		{
			LO_WORD(GATT_UUID_PRIMARY_SERVICE),
			HI_WORD(GATT_UUID_PRIMARY_SERVICE),
			LO_WORD(HID_SRV_UUID),
			HI_WORD(HID_SRV_UUID),
		},
		UUID_16BIT_SIZE,
		NULL,
		GATT_PERM_READ
	},

	/* 1: Protocol Mode characteristic declaration */
	{
		ATTRIB_FLAG_VALUE_INCL,
		{
			LO_WORD(GATT_UUID_CHARACTERISTIC),
			HI_WORD(GATT_UUID_CHARACTERISTIC),
			(GATT_CHAR_PROP_READ | GATT_CHAR_PROP_WRITE_NO_RSP),
		},
		1,
		NULL,
		GATT_PERM_READ
	},
	/* 2: Protocol Mode value */
	{
		ATTRIB_FLAG_VALUE_APPL,
		{
			LO_WORD(PROTOCOL_MODE_CHAR_UUID),
			HI_WORD(PROTOCOL_MODE_CHAR_UUID),
		},
		0,
		NULL,
		(GATT_PERM_READ_AUTHEN_REQ | GATT_PERM_WRITE_AUTHEN_REQ)
	},

	/* 3: Report Map characteristic declaration */
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
	/* 4: Report Map value */
	{
		ATTRIB_FLAG_VALUE_APPL,
		{
			LO_WORD(REPORT_MAP_CHAR_UUID),
			HI_WORD(REPORT_MAP_CHAR_UUID),
		},
		0,
		NULL,
		GATT_PERM_READ_AUTHEN_REQ
	},
	/* 5: External Report Reference descriptor */
	{
		ATTRIB_FLAG_VALUE_APPL,
		{
			LO_WORD(GATT_UUID_CHAR_EXTERNAL_REPORT_REFERENCE),
			HI_WORD(GATT_UUID_CHAR_EXTERNAL_REPORT_REFERENCE),
		},
		0,
		NULL,
		GATT_PERM_READ_AUTHEN_REQ
	},

	/* 6: Report (Input) characteristic declaration */
	{
		ATTRIB_FLAG_VALUE_INCL,
		{
			LO_WORD(GATT_UUID_CHARACTERISTIC),
			HI_WORD(GATT_UUID_CHARACTERISTIC),
			(GATT_CHAR_PROP_READ | GATT_CHAR_PROP_WRITE | GATT_CHAR_PROP_NOTIFY),
		},
		1,
		NULL,
		GATT_PERM_READ
	},
	/* 7: Input Report value */
	{
		ATTRIB_FLAG_VALUE_APPL,
		{
			LO_WORD(REPORT_CHAR_UUID),
			HI_WORD(REPORT_CHAR_UUID),
		},
		0,
		NULL,
		(GATT_PERM_READ_AUTHEN_REQ | GATT_PERM_WRITE_AUTHEN_REQ)
	},
	/* 8: Input Report CCCD */
	{
		(ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL),
		{
			LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
			HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
			LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT),
			HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT),
		},
		2,
		NULL,
		(GATT_PERM_READ | GATT_PERM_WRITE)
	},
	/* 9: Input Report Reference descriptor (report_id, report_type) */
	{
		ATTRIB_FLAG_VALUE_INCL,
		{
			LO_WORD(GATT_UUID_CHAR_REPORT_REFERENCE),
			HI_WORD(GATT_UUID_CHAR_REPORT_REFERENCE),
			HID_CC_REPORT_ID,
			HID_INPUT_TYPE,
		},
		2,
		NULL,
		GATT_PERM_READ_AUTHEN_REQ
	},

	/* 10: HID Information characteristic declaration */
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
	/* 11: HID Information value */
	{
		ATTRIB_FLAG_VALUE_APPL,
		{
			LO_WORD(HID_INFO_CHAR_UUID),
			HI_WORD(HID_INFO_CHAR_UUID),
		},
		0,
		NULL,
		GATT_PERM_READ_AUTHEN_REQ
	},

	/* 12: HID Control Point characteristic declaration */
	{
		ATTRIB_FLAG_VALUE_INCL,
		{
			LO_WORD(GATT_UUID_CHARACTERISTIC),
			HI_WORD(GATT_UUID_CHARACTERISTIC),
			GATT_CHAR_PROP_WRITE_NO_RSP,
		},
		1,
		NULL,
		GATT_PERM_READ
	},
	/* 13: HID Control Point value */
	{
		ATTRIB_FLAG_VALUE_APPL,
		{
			LO_WORD(HID_CONTROL_POINT_CHAR_UUID),
			HI_WORD(HID_CONTROL_POINT_CHAR_UUID),
		},
		0,
		NULL,
		GATT_PERM_WRITE_AUTHEN_REQ
	},
};

/* Map an attribute index to a human-readable name */
static const char *bt_ext_hids_cc_attr_name(uint16_t attrib_index)
{
	switch (attrib_index) {
	case BT_EXT_HID_PROTOCOL_MODE_VAL_INDEX:
		return "Protocol Mode";
	case BT_EXT_HID_REPORT_MAP_VAL_INDEX:
		return "Report Map";
	case BT_EXT_HID_EXT_REPORT_REF_INDEX:
		return "External Report Reference";
	case BT_EXT_HID_REPORT_INPUT_VAL_INDEX:
		return "Input Report";
	case BT_EXT_HID_REPORT_INPUT_CCCD_INDEX:
		return "Input Report CCCD";
	case BT_EXT_HID_REPORT_REF_INDEX:
		return "Input Report Reference";
	case BT_EXT_HID_INFO_VAL_INDEX:
		return "HID Info";
	case BT_EXT_HID_CONTROL_POINT_VAL_INDEX:
		return "HID Control Point";
	default:
		return "Unknown";
	}
}

/* ---- service callbacks --------------------------------------------------- */
static T_APP_RESULT bt_ext_hids_cc_read_cb(uint8_t conn_id, T_SERVER_ID service_id,
		uint16_t attrib_index, uint16_t offset, uint16_t *p_length, uint8_t **pp_value)
{
	T_APP_RESULT ret = APP_RESULT_SUCCESS;

	(void)service_id;

	RTK_LOGS(TAG, RTK_LOG_INFO, "HID OP read req: %s (index %d), conn_id %d, offset %d\r\n",
			 bt_ext_hids_cc_attr_name(attrib_index), attrib_index, conn_id, offset);

	switch (attrib_index) {
	case BT_EXT_HID_PROTOCOL_MODE_VAL_INDEX:
		*pp_value = &g_hid_protocol_mode;
		*p_length = sizeof(g_hid_protocol_mode);
		break;
	case BT_EXT_HID_REPORT_MAP_VAL_INDEX:
		*pp_value = (uint8_t *)g_hid_cc_report_descriptor;
		*p_length = sizeof(g_hid_cc_report_descriptor);
		break;
	case BT_EXT_HID_EXT_REPORT_REF_INDEX:
		*pp_value = NULL;
		*p_length = 0;
		break;
	case BT_EXT_HID_REPORT_INPUT_VAL_INDEX:
		*pp_value = NULL;
		*p_length = 0;
		break;
	case BT_EXT_HID_INFO_VAL_INDEX:
		*pp_value = (uint8_t *)&g_hid_cc_info;
		*p_length = sizeof(g_hid_cc_info);
		break;
	default:
		RTK_LOGS(TAG, RTK_LOG_ERROR, "HID read unknown index %d\r\n", attrib_index);
		ret = APP_RESULT_ATTR_NOT_FOUND;
		break;
	}

	if (ret == APP_RESULT_SUCCESS) {
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID OP read rsp: %s, len %d\r\n",
				 bt_ext_hids_cc_attr_name(attrib_index), *p_length);
	}

	return ret;
}

static T_APP_RESULT bt_ext_hids_cc_write_cb(uint8_t conn_id, T_SERVER_ID service_id,
		uint16_t attrib_index, T_WRITE_TYPE write_type, uint16_t length, uint8_t *p_value,
		P_FUN_WRITE_IND_POST_PROC *p_write_post_proc)
{
	T_APP_RESULT ret = APP_RESULT_SUCCESS;

	(void)service_id;
	(void)p_write_post_proc;

	RTK_LOGS(TAG, RTK_LOG_INFO, "HID OP write req: %s (index %d), conn_id %d, write_type %d, len %d\r\n",
			 bt_ext_hids_cc_attr_name(attrib_index), attrib_index, conn_id, write_type, length);

	if (length != 0 && p_value == NULL) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "HID write invalid value size, index %d, len %d\r\n",
				 attrib_index, length);
		return APP_RESULT_INVALID_VALUE_SIZE;
	}

	switch (attrib_index) {
	case BT_EXT_HID_PROTOCOL_MODE_VAL_INDEX:
		if (length != 1) {
			ret = APP_RESULT_INVALID_VALUE_SIZE;
			break;
		}
		g_hid_protocol_mode = p_value[0];
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID protocol mode -> %d\r\n", g_hid_protocol_mode);
		break;
	case BT_EXT_HID_CONTROL_POINT_VAL_INDEX:
		if (length != 1) {
			ret = APP_RESULT_INVALID_VALUE_SIZE;
			break;
		}
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID control point -> 0x%02x\r\n", p_value[0]);
		break;
	default:
		RTK_LOGS(TAG, RTK_LOG_ERROR, "HID write unknown index %d\r\n", attrib_index);
		ret = APP_RESULT_ATTR_NOT_FOUND;
		break;
	}

	return ret;
}

static void bt_ext_hids_cc_cccd_update_cb(uint8_t conn_id, T_SERVER_ID service_id,
		uint16_t attrib_index, uint16_t ccc_bits)
{
	(void)service_id;

	if (conn_id >= BT_EXT_HID_MAX_LINKS) {
		return;
	}

	RTK_LOGS(TAG, RTK_LOG_INFO, "HID OP cccd update: %s (index %d), conn_id %d, ccc_bits 0x%04x\r\n",
			 bt_ext_hids_cc_attr_name(attrib_index), attrib_index, conn_id, ccc_bits);

	switch (attrib_index) {
	case BT_EXT_HID_REPORT_INPUT_CCCD_INDEX:
		if (ccc_bits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) {
			g_hid_cc_ntf_en[conn_id] = 1;
			RTK_LOGS(TAG, RTK_LOG_INFO, "HID input notify enabled, conn_id %d\r\n", conn_id);
		} else {
			g_hid_cc_ntf_en[conn_id] = 0;
			RTK_LOGS(TAG, RTK_LOG_INFO, "HID input notify disabled, conn_id %d\r\n", conn_id);
		}
		break;
	default:
		break;
	}
}

static const T_FUN_GATT_SERVICE_CBS bt_ext_hids_cc_cbs = {
	bt_ext_hids_cc_read_cb,     /* read_attr_cb  */
	bt_ext_hids_cc_write_cb,    /* write_attr_cb */
	bt_ext_hids_cc_cccd_update_cb,  /* cccd_update_cb */
};

T_SERVER_ID bt_ext_hids_cc_add_service(void *p_func)
{
	T_SERVER_ID service_id;

	if (false == server_add_service(&service_id,
									(uint8_t *)bt_ext_hids_cc_tbl,
									sizeof(bt_ext_hids_cc_tbl),
									bt_ext_hids_cc_cbs)) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "HID CC add service failed\r\n");
		service_id = 0xFF;
	} else {
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID OP add service success, service_id %d\r\n", service_id);
	}

	g_hids_cc_service_id = service_id;
	pfn_hids_cc_cb = (P_FUN_SERVER_GENERAL_CB)p_func;
	return service_id;
}

/* ---- send Consumer Control input reports --------------------------------- */
void bt_ext_hids_cc_send_key(uint8_t conn_id, uint16_t key_bitmap)
{
	/* The CC input report is a single byte (REPORT_SIZE 1 x REPORT_COUNT 8). */
	uint8_t report = (uint8_t)(key_bitmap & 0xFF);

	if (g_hids_cc_service_id == 0xFF) {
		RTK_LOGS(TAG, RTK_LOG_WARN, "HID send key skipped: service not registered\r\n");
		return;
	}

	if (conn_id >= BT_EXT_HID_MAX_LINKS) {
		RTK_LOGS(TAG, RTK_LOG_WARN, "HID send key skipped: bad conn_id %d\r\n", conn_id);
		return;
	}

	if (!g_hid_cc_ntf_en[conn_id]) {
		RTK_LOGS(TAG, RTK_LOG_WARN,
				 "HID send key skipped: notify not enabled, conn_id %d, key 0x%02x\r\n",
				 conn_id, report);
		return;
	}

	if (server_send_data(conn_id, g_hids_cc_service_id, BT_EXT_HID_REPORT_INPUT_VAL_INDEX,
						 &report, sizeof(report), GATT_PDU_TYPE_NOTIFICATION) == false) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "HID OP send notify failed, conn_id %d, key 0x%02x\r\n",
				 conn_id, report);
	} else {
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID OP send notify: Input Report, conn_id %d, key 0x%02x\r\n",
				 conn_id, report);
	}
}

void bt_ext_hids_cc_next_track(uint8_t conn_id, bool press)
{
	bt_ext_hids_cc_send_key(conn_id, press ? (1 << BT_EXT_HID_CC_SCAN_NEXT_TRACK) : 0);
}

void bt_ext_hids_cc_prev_track(uint8_t conn_id, bool press)
{
	bt_ext_hids_cc_send_key(conn_id, press ? (1 << BT_EXT_HID_CC_SCAN_PREV_TRACK) : 0);
}

void bt_ext_hids_cc_play_pause(uint8_t conn_id, bool press)
{
	bt_ext_hids_cc_send_key(conn_id, press ? (1 << BT_EXT_HID_CC_PLAY_PAUSE) : 0);
}

#endif /* CONFIG_BT_EXT */
