/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth SPP (Serial Port Profile) server implementation.
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>

#include "ameba_soc.h"
#include "btm.h"
#include "bt_spp.h"

#include "bt_classic_spp.h"
#include "bt_classic_link.h"

static const char *const TAG = "BTEXT_SPP";

#define BT_CLASSIC_SPP_LINK_NUM         1
#define BT_CLASSIC_SPP_SERVICE_NUM      1

/* Credit flow control: initial credits granted to peer; replenished per received frame. */
#define BT_CLASSIC_SPP_RX_CREDITS       10
#define BT_CLASSIC_SPP_RX_REPLENISH     1

#define BT_CLASSIC_SPP_LOG_DUMP_MAX     48

static const uint8_t spp_serial_port_uuid128[16] = {
	0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb
};

static struct {
	bool     connected;
	uint8_t  bd_addr[6];
	uint8_t  local_server_chann;
	uint8_t  tx_credit;
	uint16_t frame_size;
} s_spp;

static void bt_classic_spp_log_rx(const uint8_t *data, uint16_t len)
{
	char     buf[BT_CLASSIC_SPP_LOG_DUMP_MAX + 1];
	uint16_t n = (len < BT_CLASSIC_SPP_LOG_DUMP_MAX) ? len : BT_CLASSIC_SPP_LOG_DUMP_MAX;
	uint16_t i;

	for (i = 0; i < n; i++) {
		uint8_t c = data[i];
		buf[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
	}
	buf[n] = '\0';

	RTK_LOGS(TAG, RTK_LOG_INFO, ">>> SPP RX %d bytes: \"%s\"%s <<<\r\n",
			 len, buf, (len > n) ? " ..." : "");
}

void bt_classic_spp_handle_event(T_BT_EVENT event_type, void *event_buf,
								 uint16_t buf_len)
{
	T_BT_EVENT_PARAM *param = event_buf;

	(void)buf_len;

	switch (event_type) {
	case BT_EVENT_SPP_CONN_IND: {
		bool ok = bt_spp_connect_cfm(param->spp_conn_ind.bd_addr,
									 param->spp_conn_ind.local_server_chann,
									 true,
									 param->spp_conn_ind.frame_size,
									 BT_CLASSIC_SPP_RX_CREDITS);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"SPP conn ind " BD_FMT " chann 0x%02x frame %d: accept cfm=%d\r\n",
			BD_ARG(param->spp_conn_ind.bd_addr),
			param->spp_conn_ind.local_server_chann,
			param->spp_conn_ind.frame_size, ok);
		break;
	}

	case BT_EVENT_SPP_CONN_CMPL:
		memset(&s_spp, 0, sizeof(s_spp));
		s_spp.connected = true;
		memcpy(s_spp.bd_addr, param->spp_conn_cmpl.bd_addr, 6);
		s_spp.local_server_chann = param->spp_conn_cmpl.local_server_chann;
		s_spp.frame_size = param->spp_conn_cmpl.frame_size;
		s_spp.tx_credit = param->spp_conn_cmpl.link_credit;
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> SPP CONNECTED " BD_FMT " chann 0x%02x frame %d tx_credit %d <<<\r\n",
			BD_ARG(param->spp_conn_cmpl.bd_addr),
			s_spp.local_server_chann, s_spp.frame_size, s_spp.tx_credit);
		break;

	case BT_EVENT_SPP_CONN_FAIL:
		RTK_LOGS(TAG, RTK_LOG_ERROR,
			">>> SPP CONN FAIL " BD_FMT ", cause 0x%04x <<<\r\n",
			BD_ARG(param->spp_conn_fail.bd_addr),
			param->spp_conn_fail.cause);
		break;

	case BT_EVENT_SPP_CREDIT_RCVD:
		if (s_spp.connected &&
			memcmp(s_spp.bd_addr, param->spp_credit_rcvd.bd_addr, 6) == 0) {
			s_spp.tx_credit = param->spp_credit_rcvd.link_credit;
		}
		RTK_LOGS(TAG, RTK_LOG_INFO, "SPP credit rcvd: tx_credit %d\r\n",
				 param->spp_credit_rcvd.link_credit);
		break;

	case BT_EVENT_SPP_DATA_IND:
		bt_classic_spp_log_rx(param->spp_data_ind.data, param->spp_data_ind.len);
		/* TODO: hand RX data to business logic (e.g. command parsing). */

		bt_spp_credits_give(param->spp_data_ind.bd_addr,
							param->spp_data_ind.local_server_chann,
							BT_CLASSIC_SPP_RX_REPLENISH);
		break;

	case BT_EVENT_SPP_DISCONN_CMPL:
		if (memcmp(s_spp.bd_addr, param->spp_disconn_cmpl.bd_addr, 6) == 0) {
			memset(&s_spp, 0, sizeof(s_spp));
		}
		RTK_LOGS(TAG, RTK_LOG_INFO,
			">>> SPP DISCONNECTED " BD_FMT " chann 0x%02x, cause 0x%04x <<<\r\n",
			BD_ARG(param->spp_disconn_cmpl.bd_addr),
			param->spp_disconn_cmpl.local_server_chann,
			param->spp_disconn_cmpl.cause);
		break;

	default:
		if ((event_type & 0xFF00) == 0x3000) {
			RTK_LOGS(TAG, RTK_LOG_INFO, "spp event 0x%04x\r\n", event_type);
		}
		break;
	}
}

bool bt_classic_spp_send(uint8_t *data, uint16_t len)
{
	if (!s_spp.connected) {
		RTK_LOGS(TAG, RTK_LOG_WARN, "SPP send: not connected\r\n");
		return false;
	}
	if (len > s_spp.frame_size) {
		RTK_LOGS(TAG, RTK_LOG_WARN, "SPP send: len %d > frame_size %d\r\n",
				 len, s_spp.frame_size);
		return false;
	}
	if (s_spp.tx_credit == 0) {
		RTK_LOGS(TAG, RTK_LOG_WARN, "SPP send: no tx credit\r\n");
		return false;
	}

	if (bt_spp_data_send(s_spp.bd_addr, s_spp.local_server_chann, data, len, false)) {
		s_spp.tx_credit--;
		return true;
	}
	RTK_LOGS(TAG, RTK_LOG_ERROR, "SPP send: bt_spp_data_send failed\r\n");
	return false;
}

void bt_classic_spp_init(void)
{
	bool ok_init, ok_reg, ok_ertm;

	memset(&s_spp, 0, sizeof(s_spp));

	ok_init = bt_spp_init(BT_CLASSIC_SPP_LINK_NUM, BT_CLASSIC_SPP_SERVICE_NUM);

	ok_reg = bt_spp_service_register((uint8_t *)spp_serial_port_uuid128,
									 BT_CLASSIC_SPP_SERVER_CHANN);

	ok_ertm = bt_spp_ertm_mode_set(false);

	RTK_LOGS(TAG, RTK_LOG_INFO,
		"SPP server init: chann 0x%02x, spp_init=%d service_register=%d ertm_set=%d\r\n",
		BT_CLASSIC_SPP_SERVER_CHANN, ok_init, ok_reg, ok_ertm);
}

#endif /* CONFIG_BT_EXT */
