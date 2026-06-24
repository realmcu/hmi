/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Dashboard BT EXT coordinator task entry.
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
#include "gap_br.h"
#include "gap_config.h"   /* gap_config_br_link_num: must be called before bte_init */
#include "trace_app.h"
#include "app_msg.h"
#include "sysm.h"
#include "remote.h"
#include "bt_sdp.h"

#include "dashboard_bt_ext.h"
#include "ble/dashboard_ble.h"
#include "bt_classic/dashboard_bt_classic.h"
#include "io_peripheral/dashboard_key.h"

static const char *const TAG = "BTEXT_COORD";

/* Compile-time switches: set to 0 to disable a sub-module. */
#ifndef ENABLE_BLE_EXT
#define ENABLE_BLE_EXT          0
#endif
#ifndef ENABLE_BT_CLASSIC_EXT
#define ENABLE_BT_CLASSIC_EXT   1
#endif

#define MAX_NUMBER_OF_GAP_MSG      0x20
#define MAX_NUMBER_OF_IO_MSG       0x40

#define BT_HID_DEMO_DEFAULT_PAGESCAN_WINDOW             0x200
#define BT_HID_DEMO_DEFAULT_PAGESCAN_INTERVAL           0x800
#define BT_HID_DEMO_DEFAULT_PAGE_TIMEOUT                0x8000
#define BT_HID_DEMO_DEFAULT_SUPVISIONTIMEOUT            0x1f40
#define BT_HID_DEMO_DEFAULT_INQUIRYSCAN_WINDOW          0x200
#define BT_HID_DEMO_DEFAULT_INQUIRYSCAN_INTERVAL        0x800

static void *ble_evt_queue = NULL;
static void *ble_io_queue  = NULL;

static bool ble_post_io_msg(T_IO_MSG *p_msg)
{
	uint8_t event = EVENT_IO_TO_APP;

	if (os_msg_send(ble_io_queue, p_msg, 0) == false) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "post io msg failed\r\n");
		return false;
	}
	if (os_msg_send(ble_evt_queue, &event, 0) == false) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "post evt failed\r\n");
		return false;
	}
	return true;
}

static void bt_ext_key_hook(key_event_t event)
{
	T_IO_MSG io_msg;

	io_msg.type    = IO_MSG_TYPE_GPIO;
	io_msg.subtype = IO_MSG_GPIO_KEY;
	io_msg.u.param = (uint32_t)event;

	ble_post_io_msg(&io_msg);
}

#if ENABLE_BT_CLASSIC_EXT
static void bt_mgr_cback(T_BT_EVENT event_type, void *event_buf, uint16_t buf_len)
{
	dashboard_bt_classic_btmgr_callback(event_type, event_buf, buf_len);
}
#endif

void dash_board_bt_ext_task(void *param)
{
	uint8_t event;

	(void)param;

	RTK_LOGS(TAG, RTK_LOG_INFO, "BT EXT coordinator starting...\r\n");

	bt_trace_init();

	/* Stack config below must precede bte_init. */
	/* 2 ACL links: A2DP relay needs phone + headphone connected at once. */
	gap_config_br_link_num(2);

	{
		extern void gap_config_br_l2c_chann_num(uint8_t br_l2c_chann_num);
		gap_config_br_l2c_chann_num(12);

		extern void gap_config_sco_link_num(uint8_t sco_link_num);
		gap_config_sco_link_num(4);
	}

	gap_config_bte_pool_size(10);

	if (!bte_init()) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "bte_init failed, exiting task\r\n");
		rtos_task_delete(NULL);
		return;
	}
	{
		extern unsigned char l2c_get_free_chann_num(unsigned char link_type);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> L2CAP classic channel pool after bte_init: free=%d (should = br_l2c_chann_num) <<<\r\n",
			l2c_get_free_chann_num(0));
	}


#if ENABLE_BLE_EXT
	if (dashboard_ble_init(&ble_evt_queue, &ble_io_queue) != 0) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "BLE init failed, exiting task\r\n");
		rtos_task_delete(NULL);
		return;
	}
#else
	/* BLE disabled: create the queues here (normally done by dashboard_ble_init). */
	RTK_LOGS(TAG, RTK_LOG_INFO, "BLE EXT disabled by ENABLE_BLE_EXT=0\r\n");
	os_msg_queue_create(&ble_io_queue, MAX_NUMBER_OF_IO_MSG, sizeof(T_IO_MSG));
	os_msg_queue_create(&ble_evt_queue,
		MAX_NUMBER_OF_GAP_MSG + MAX_NUMBER_OF_IO_MSG, sizeof(uint8_t));
	if (!ble_io_queue || !ble_evt_queue) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "msg queue create failed\r\n");
		rtos_task_delete(NULL);
		return;
	}
#endif

#if ENABLE_BT_CLASSIC_EXT
	/* bt_mgr_init() (inside dashboard_bt_classic_init) requires these first. */
	sys_mgr_init(ble_evt_queue);
	remote_mgr_init(REMOTE_SESSION_ROLE_SINGLE);

	if (dashboard_bt_classic_init() != 0) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "BT Classic init failed, exiting task\r\n");
		rtos_task_delete(NULL);
		return;
	}

	/* Register callback after bt_mgr_init, before HID profile. */
	if (!bt_mgr_cback_register(bt_mgr_cback)) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "bt_mgr_cback_register failed\r\n");
		rtos_task_delete(NULL);
		return;
	}

	if (dashboard_bt_classic_profile_init() != 0) {
		RTK_LOGS(TAG, RTK_LOG_ERROR,
			"BT Classic HID+SDP init failed, exiting task\r\n");
		rtos_task_delete(NULL);
		return;
	}

#endif /* ENABLE_BT_CLASSIC_EXT */

	gap_start_bt_stack(ble_evt_queue, ble_io_queue,
					   MAX_NUMBER_OF_GAP_MSG);
	RTK_LOGS(TAG, RTK_LOG_INFO, ">>> STACK STARTED <<<\r\n");

	{
		extern void hci_get_baudrate(uint8_t *baudrate, bool use_default_rate);
		uint8_t baud[4];
		uint32_t rate = 0;
		
		hci_get_baudrate(baud, false);
		do {
			if (baud[0]==0x02 && baud[1]==0x80 && baud[2]==0x92 && baud[3]==0x04) { rate = 1500000; break; }
			if (baud[0]==0x1d && baud[1]==0x70 && baud[2]==0x00 && baud[3]==0x00) { rate = 115200; break; }
			if (baud[0]==0x0a && baud[1]==0xc0 && baud[2]==0x52 && baud[3]==0x02) { rate = 230400; break; }
			if (baud[0]==0x04 && baud[1]==0x50 && baud[2]==0x00 && baud[3]==0x00) { rate = 1000000; break; }
			if (baud[0]==0x01 && baud[1]==0x80 && baud[2]==0x92 && baud[3]==0x04) { rate = 3000000; break; }
		} while(0);
		RTK_LOGS(TAG, RTK_LOG_INFO, ">>> HCI work baudrate %u bps (%02x %02x %02x %02x) <<<\r\n",
			(unsigned int)rate, baud[0], baud[1], baud[2], baud[3]);
	}
#if ENABLE_BLE_EXT
	dashboard_ble_start();
	RTK_LOGS(TAG, RTK_LOG_INFO, "BLE advertising started\r\n");
#endif
#if !ENABLE_BLE_EXT
	RTK_LOGS(TAG, RTK_LOG_INFO, "BLE disabled, skip advertising\r\n");
#endif

	key_set_send_hook(bt_ext_key_hook);
	// dashboard_button_init();

	RTK_LOGS(TAG, RTK_LOG_INFO, ">>> MESSAGE LOOP RUNNING <<<\r\n");
	RTK_LOGS(TAG, RTK_LOG_INFO, "ENABLED: BLE=%d  BT_CLASSIC=%d\r\n", ENABLE_BLE_EXT, ENABLE_BT_CLASSIC_EXT);
	while (true) {
		if (os_msg_recv(ble_evt_queue, &event, 0xFFFFFFFF) == true) {
			if (EVENT_GROUP(event) == EVENT_GROUP_IO) {
				if (event == EVENT_IO_TO_APP) {
					T_IO_MSG io_msg;
					if (os_msg_recv(ble_io_queue, &io_msg, 0) == true) {
						if (io_msg.type == IO_MSG_TYPE_GPIO &&
							io_msg.subtype == IO_MSG_GPIO_KEY) {
							key_event_t ke = (key_event_t)io_msg.u.param;
#if ENABLE_BLE_EXT
							dashboard_ble_key_handler(ke);
#endif
#if ENABLE_BT_CLASSIC_EXT
							dashboard_bt_classic_key_handler(ke);
#endif
						} else {
#if ENABLE_BLE_EXT
							dashboard_ble_gap_handler(&io_msg);
#else
							RTK_LOGS(TAG, RTK_LOG_WARN,
								"unexpected io_msg type=%d subtype=%d (BLE disabled)\r\n",
								io_msg.type, io_msg.subtype);
#endif
						}
					}
				}
			} else if (EVENT_GROUP(event) == EVENT_GROUP_STACK) {
				gap_handle_msg(event);
			} else if (EVENT_GROUP(event) == EVENT_GROUP_FRAMEWORK) {
				sys_mgr_event_handle(event);
			}
		}
	}
}

#endif /* CONFIG_BT_EXT */