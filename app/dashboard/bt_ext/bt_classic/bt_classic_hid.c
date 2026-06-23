/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth HID consumer-control (media keys) device.
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>

#include "ameba_soc.h"
#include "btm.h"
#include "bt_hid.h"
#include "bt_sdp.h"
#include "bt_types.h"

#include "bt_classic_hid.h"
#include "bt_classic_link.h"

static const char *const TAG = "BTEXT_HID";

/* Report ID; report buffer format is [REPORT_ID][1 key-bits byte]. */
#define BT_CLASSIC_HID_REPORT_ID    0x01

#define HID_BIT_NEXT_TRACK          0x01   /* bit0: Scan Next Track */
#define HID_BIT_PREV_TRACK          0x02   /* bit1: Scan Previous Track */
#define HID_BIT_PLAY_PAUSE          0x04   /* bit2: Play/Pause */

/* HID report descriptor (Consumer Control, 29 bytes). Must stay byte-identical
 * to the descriptor embedded in the SDP record below. */
static const uint8_t bt_classic_hid_report_desc[] = {
	0x05, 0x0C,        /* Usage Page (Consumer)            */
	0x09, 0x01,        /* Usage (Consumer Control)         */
	0xA1, 0x01,        /* Collection (Application)         */
	0x85, 0x01,        /*   Report ID (1)                  */
	0x15, 0x00,        /*   Logical Minimum (0)            */
	0x25, 0x01,        /*   Logical Maximum (1)            */
	0x75, 0x01,        /*   Report Size (1)                */
	0x95, 0x03,        /*   Report Count (3)               */
	0x09, 0xB5,        /*   Usage (Scan Next Track)        */
	0x09, 0xB6,        /*   Usage (Scan Previous Track)    */
	0x09, 0xCD,        /*   Usage (Play/Pause)             */
	0x81, 0x02,        /*   Input (Data,Var,Abs)           */
	0x95, 0x05,        /*   Report Count (5)               */
	0x81, 0x01,        /*   Input (Const,Array,Abs) padding */
	0xC0               /* End Collection                   */
};

/* HID SDP service record. Content length 0x00DD (221 bytes); total array 224. */
static const uint8_t bt_classic_hid_sdp_record[] = {
	SDP_DATA_ELEM_SEQ_HDR_2BYTE, 0x00, 0xDD,

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)(SDP_ATTR_SRV_CLASS_ID_LIST >> 8), (uint8_t)SDP_ATTR_SRV_CLASS_ID_LIST,
	SDP_DATA_ELEM_SEQ_HDR, 0x03,
	SDP_UUID16_HDR,
	(uint8_t)(UUID_HUMAN_INTERFACE_DEVICE_SERVICE >> 8),
	(uint8_t)UUID_HUMAN_INTERFACE_DEVICE_SERVICE,

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)(SDP_ATTR_PROTO_DESC_LIST >> 8), (uint8_t)SDP_ATTR_PROTO_DESC_LIST,
	SDP_DATA_ELEM_SEQ_HDR, 0x0D,
	SDP_DATA_ELEM_SEQ_HDR, 0x06,
	SDP_UUID16_HDR, (uint8_t)(UUID_L2CAP >> 8), (uint8_t)UUID_L2CAP,
	SDP_UNSIGNED_TWO_BYTE, (uint8_t)(PSM_HID_CONTROL >> 8), (uint8_t)PSM_HID_CONTROL,
	SDP_DATA_ELEM_SEQ_HDR, 0x03,
	SDP_UUID16_HDR, (uint8_t)(UUID_HIDP >> 8), (uint8_t)UUID_HIDP,

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)(SDP_ATTR_LANG_BASE_ATTR_ID_LIST >> 8), (uint8_t)SDP_ATTR_LANG_BASE_ATTR_ID_LIST,
	SDP_DATA_ELEM_SEQ_HDR, 0x09,
	SDP_UNSIGNED_TWO_BYTE, (uint8_t)(SDP_LANG_ENGLISH >> 8), (uint8_t)SDP_LANG_ENGLISH,
	SDP_UNSIGNED_TWO_BYTE, (uint8_t)(SDP_CHARACTER_UTF8 >> 8), (uint8_t)SDP_CHARACTER_UTF8,
	SDP_UNSIGNED_TWO_BYTE, (uint8_t)(SDP_BASE_LANG_OFFSET >> 8), (uint8_t)SDP_BASE_LANG_OFFSET,

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)(SDP_ATTR_PROFILE_DESC_LIST >> 8), (uint8_t)SDP_ATTR_PROFILE_DESC_LIST,
	SDP_DATA_ELEM_SEQ_HDR, 0x08,
	SDP_DATA_ELEM_SEQ_HDR, 0x06,
	SDP_UUID16_HDR,
	(uint8_t)(UUID_HUMAN_INTERFACE_DEVICE_SERVICE >> 8),
	(uint8_t)UUID_HUMAN_INTERFACE_DEVICE_SERVICE,
	SDP_UNSIGNED_TWO_BYTE, 0x01, 0x01,

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)(SDP_ATTR_ADD_PROTO_DESC_LIST >> 8), (uint8_t)SDP_ATTR_ADD_PROTO_DESC_LIST,
	SDP_DATA_ELEM_SEQ_HDR, 0x0F,
	SDP_DATA_ELEM_SEQ_HDR, 0x0D,
	SDP_DATA_ELEM_SEQ_HDR, 0x06,
	SDP_UUID16_HDR, (uint8_t)(UUID_L2CAP >> 8), (uint8_t)UUID_L2CAP,
	SDP_UNSIGNED_TWO_BYTE, (uint8_t)(PSM_HID_INTERRUPT >> 8), (uint8_t)PSM_HID_INTERRUPT,
	SDP_DATA_ELEM_SEQ_HDR, 0x03,
	SDP_UUID16_HDR, (uint8_t)(UUID_HIDP >> 8), (uint8_t)UUID_HIDP,

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)((SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET) >> 8),
	(uint8_t)(SDP_ATTR_SRV_NAME + SDP_BASE_LANG_OFFSET),
	SDP_STRING_HDR, 0x0B,
	'R', 'e', 'a', 'l', 't', 'e', 'k', ' ', 'H', 'I', 'D',

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)((SDP_ATTR_SRV_DESC + SDP_BASE_LANG_OFFSET) >> 8),
	(uint8_t)(SDP_ATTR_SRV_DESC + SDP_BASE_LANG_OFFSET),
	SDP_STRING_HDR, 0x10,
	'M', 'e', 'd', 'i', 'a', ' ', 'R', 'e', 'm', 'o', 't', 'e', ' ', 'C', 't', 'l',

	SDP_UNSIGNED_TWO_BYTE,
	(uint8_t)((SDP_ATTR_PROVIDER_NAME + SDP_BASE_LANG_OFFSET) >> 8),
	(uint8_t)(SDP_ATTR_PROVIDER_NAME + SDP_BASE_LANG_OFFSET),
	SDP_STRING_HDR, 0x07,
	'R', 'e', 'a', 'l', 't', 'e', 'k',

	/* HIDParserVersion (0x0201) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x01,
	SDP_UNSIGNED_TWO_BYTE, 0x01, 0x11,

	/* HIDDeviceSubclass (0x0202) = 0x40 (keyboard) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x02,
	SDP_UNSIGNED_ONE_BYTE, 0x40,

	/* HIDCountryCode (0x0203) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x03,
	SDP_UNSIGNED_ONE_BYTE, 0x21,

	/* HIDVirtualCable (0x0204) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x04,
	SDP_BOOL_ONE_BYTE, 0x01,

	/* HIDReconnectInitiate (0x0205) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x05,
	SDP_BOOL_ONE_BYTE, 0x01,

	/* HIDDescriptorList (0x0206): embedded 29-byte report descriptor */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x06,
	SDP_DATA_ELEM_SEQ_HDR, 0x23,
	SDP_DATA_ELEM_SEQ_HDR, 0x21,
	SDP_UNSIGNED_ONE_BYTE, 0x22,           /* descriptor type = Report Descriptor */
	SDP_STRING_HDR, 0x1D,                  /* 29 bytes */
	0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x01, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x03, 0x09, 0xB5, 0x09, 0xB6, 0x09, 0xCD, 0x81, 0x02,
	0x95, 0x05, 0x81, 0x01, 0xC0,

	/* HIDLANGIDBaseList (0x0207) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x07,
	SDP_DATA_ELEM_SEQ_HDR, 0x08,
	SDP_DATA_ELEM_SEQ_HDR, 0x06,
	SDP_UNSIGNED_TWO_BYTE, 0x04, 0x09,
	SDP_UNSIGNED_TWO_BYTE, 0x01, 0x00,

	/* HIDBatteryPower (0x0209) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x09,
	SDP_BOOL_ONE_BYTE, 0x01,

	/* HIDRemoteWake (0x020A) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x0A,
	SDP_BOOL_ONE_BYTE, 0x01,

	/* HIDNormallyConnectable (0x020D) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x0D,
	SDP_BOOL_ONE_BYTE, 0x01,

	/* HIDBootDevice (0x020E) = false (no Boot Protocol) */
	SDP_UNSIGNED_TWO_BYTE, 0x02, 0x0E,
	SDP_BOOL_ONE_BYTE, 0x00,
};

/* Compile-time size asserts: descriptor 29 bytes, SDP record 224 bytes. */
typedef char _bt_classic_hid_desc_size_check[(sizeof(bt_classic_hid_report_desc) == 29) ? 1 : -1];
typedef char _bt_classic_hid_sdp_size_check[(sizeof(bt_classic_hid_sdp_record) == 224) ? 1 : -1];

/* Current HID connection context (single link). */
static struct {
	bool    connected;
	uint8_t bd_addr[6];
	uint8_t key_bits;     /* pressed media-key bitmap (bit0/1/2) */
} s_hid;

#define BD_FMT          "%02x:%02x:%02x:%02x:%02x:%02x"
#define BD_ARG(a)       (a)[5], (a)[4], (a)[3], (a)[2], (a)[1], (a)[0]

static void bt_classic_hid_send_report(void)
{
	uint8_t rpt[2];

	if (!s_hid.connected) {
		return;
	}
	rpt[0] = BT_CLASSIC_HID_REPORT_ID;
	rpt[1] = s_hid.key_bits;

	if (bt_hid_interrupt_data_send(s_hid.bd_addr, BT_HID_REPORT_TYPE_INPUT,
								   rpt, sizeof(rpt))) {
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID report sent: 0x%02x\r\n", s_hid.key_bits);
	} else {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "HID report send failed (bits 0x%02x)\r\n",
				 s_hid.key_bits);
	}
}

void bt_classic_hid_handle_event(T_BT_EVENT event_type, void *event_buf,
								 uint16_t buf_len)
{
	T_BT_EVENT_PARAM *param = event_buf;

	(void)buf_len;

	switch (event_type) {
	case BT_EVENT_HID_CONN_IND: {
		bool ok = bt_hid_connect_cfm(param->hid_conn_ind.bd_addr, true);
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID conn ind " BD_FMT ": accept cfm=%d\r\n",
				 BD_ARG(param->hid_conn_ind.bd_addr), ok);
		break;
	}

	case BT_EVENT_HID_CONN_CMPL:
		memset(&s_hid, 0, sizeof(s_hid));
		s_hid.connected = true;
		memcpy(s_hid.bd_addr, param->hid_conn_cmpl.bd_addr, 6);
		RTK_LOGS(TAG, RTK_LOG_INFO, ">>> HID CONNECTED " BD_FMT " <<<\r\n",
				 BD_ARG(param->hid_conn_cmpl.bd_addr));
		break;

	case BT_EVENT_HID_DISCONN_CMPL:
		if (memcmp(s_hid.bd_addr, param->hid_disconn_cmpl.bd_addr, 6) == 0) {
			memset(&s_hid, 0, sizeof(s_hid));
		}
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> HID DISCONNECTED " BD_FMT ", cause 0x%04x <<<\r\n",
			BD_ARG(param->hid_disconn_cmpl.bd_addr),
			param->hid_disconn_cmpl.cause);
		break;

	case BT_EVENT_HID_GET_REPORT_IND: {
		uint8_t rpt[2];

		rpt[0] = (uint8_t)param->hid_get_report_ind.report_id;
		rpt[1] = s_hid.key_bits;
		bt_hid_control_get_report_rsp(param->hid_get_report_ind.bd_addr,
			(T_BT_HID_REPORT_TYPE)param->hid_get_report_ind.report_type,
			rpt, sizeof(rpt));
		RTK_LOGS(TAG, RTK_LOG_INFO, "HID get report (id %d) -> 0x%02x\r\n",
				 param->hid_get_report_ind.report_id, s_hid.key_bits);
		break;
	}

	default:
		if ((event_type & 0xFF00) == 0x3600) {
			RTK_LOGS(TAG, RTK_LOG_INFO, "hid event 0x%04x\r\n", event_type);
		}
		break;
	}
}

void bt_classic_hid_key_handler(key_event_t event)
{
	uint8_t bit;
	bool    press;

	switch (event) {
	case KEY_NEXT_TRACK_PRESS:   bit = HID_BIT_NEXT_TRACK; press = true;  break;
	case KEY_NEXT_TRACK_RELEASE: bit = HID_BIT_NEXT_TRACK; press = false; break;
	case KEY_PREV_TRACK_PRESS:   bit = HID_BIT_PREV_TRACK; press = true;  break;
	case KEY_PREV_TRACK_RELEASE: bit = HID_BIT_PREV_TRACK; press = false; break;
	case KEY_PLAY_PAUSE_PRESS:   bit = HID_BIT_PLAY_PAUSE; press = true;  break;
	case KEY_PLAY_PAUSE_RELEASE: bit = HID_BIT_PLAY_PAUSE; press = false; break;
	default:
		return;
	}

	if (press) {
		s_hid.key_bits |= bit;
	} else {
		s_hid.key_bits &= (uint8_t)~bit;
	}

	if (!s_hid.connected) {
		RTK_LOGS(TAG, RTK_LOG_WARN,
			"key event %d but HID not connected (bits 0x%02x)\r\n",
			event, s_hid.key_bits);
		return;
	}
	bt_classic_hid_send_report();
}

void bt_classic_hid_init(void)
{
	bool ok_init, ok_desc, ok_sdp;

	memset(&s_hid, 0, sizeof(s_hid));

	/* link_num=1, boot_proto_mode=false (Report protocol only). */
	ok_init = bt_hid_init(1, false);

	ok_desc = bt_hid_descriptor_set(bt_classic_hid_report_desc,
									sizeof(bt_classic_hid_report_desc));

	ok_sdp = bt_sdp_record_add((void *)bt_classic_hid_sdp_record);

	RTK_LOGS(TAG, RTK_LOG_INFO,
		"HID consumer-control init: hid_init=%d desc_set=%d sdp_add=%d\r\n",
		ok_init, ok_desc, ok_sdp);
}

#endif /* CONFIG_BT_EXT */
