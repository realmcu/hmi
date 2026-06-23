/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth HFP call relay (signaling + SCO voice).
 * HFP_* events = HF role = phone (AG); HFP_AG_* events = AG role = headphone (HF).
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include "bt_classic_config.h"

#if BT_CLASSIC_ENABLE_HFP

#include <string.h>
#include <stdbool.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "btm.h"
#include "bt_hfp.h"
#include "bt_hfp_ag.h"
#include "bt_sdp.h"
#include "bt_types.h"
#include "gap_br.h"

#include "bt_classic_hfp.h"
#include "bt_classic_link.h"
#include "bt_classic_a2dp.h"

static const char *const TAG = "BTEXT_HFP";

/* Both ends locked to CVSD so SCO can be raw-forwarded without transcoding. */
#define HFP_HF_SUPP_CODEC   BT_HFP_HF_CODEC_TYPE_CVSD
#define HFP_AG_SUPP_CODEC   BT_HFP_AG_CODEC_TYPE_CVSD

#define HFP_HF_FEATURES   (BT_HFP_HF_LOCAL_THREE_WAY_CALLING |            \
						   BT_HFP_HF_LOCAL_CLI_PRESENTATION_CAPABILITY |   \
						   BT_HFP_HF_LOCAL_ESCO_S4_SETTINGS |              \
						   BT_HFP_HF_LOCAL_REMOTE_VOLUME_CONTROL)
/* Headphone SCO kept at basic CVSD SCO (no eSCO-S4): 8761BTV cannot schedule a
 * second retransmitting EDR eSCO while the phone eSCO is up. */
#define HFP_AG_FEATURES   (BT_HFP_AG_LOCAL_CAPABILITY_3WAY)

static struct {
	bool     phone_valid;
	uint8_t  phone_addr[6];
	uint8_t  phone_call;

	bool     hp_valid;
	uint8_t  hp_addr[6];
	uint8_t  hp_chann;

	bool     phone_sco;
	bool     hp_sco;
	uint16_t phone_sco_handle;
	uint16_t hp_sco_handle;

	char     caller_num[20];
	uint8_t  caller_type;
} s_hfp;

/* Headphone AG call must be active before the framework accepts audio connect. */
static bool hfp_call_active(uint8_t st)
{
	return st == BT_HFP_CALL_ACTIVE ||
		   st == BT_HFP_CALL_ACTIVE_WITH_CALL_WAITING ||
		   st == BT_HFP_CALL_ACTIVE_WITH_CALL_HELD;
}

/* Bring up headphone SCO (idempotent); only call once headphone call is active. */
static void hfp_try_bring_up_hp_sco(void)
{
	if (s_hfp.hp_valid && !s_hfp.hp_sco) {
		bt_hfp_ag_audio_connect_req(s_hfp.hp_addr);
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"relay: headphone SCO audio-connect issued (await SCO_CONN_CMPL[headphone])\r\n");
	}
}

static void hfp_send_ag_indicators(uint8_t *hp)
{
	bool active = (s_hfp.phone_call == BT_HFP_CALL_ACTIVE ||
				   s_hfp.phone_call == BT_HFP_CALL_ACTIVE_WITH_CALL_WAITING ||
				   s_hfp.phone_call == BT_HFP_CALL_ACTIVE_WITH_CALL_HELD);
	T_BT_HFP_AG_CALL_SETUP_INDICATOR setup =
		(s_hfp.phone_call == BT_HFP_CALL_INCOMING)
			? BT_HFP_AG_CALL_SETUP_STATUS_INCOMING_CALL
		: (s_hfp.phone_call == BT_HFP_CALL_OUTGOING)
			? BT_HFP_AG_CALL_SETUP_STATUS_OUTGOING_CALL
			: BT_HFP_AG_CALL_SETUP_STATUS_IDLE;

	bt_hfp_ag_indicators_send(
		hp,
		BT_HFP_AG_SERVICE_STATUS_AVAILABLE,
		active ? BT_HFP_AG_CALL_IN_PROGRESS : BT_HFP_AG_NO_CALL_IN_PROGRESS,
		setup,
		BT_HFP_AG_CALL_HELD_STATUS_IDLE,
		5 ,
		BT_HFP_AG_ROAMING_STATUS_INACTIVE,
		5 );
}

/* Replay phone call state changes onto the headphone (AG side). */
static void hfp_relay_phone_call_to_hp(uint8_t prev, uint8_t curr)
{
	if (!s_hfp.hp_valid) {
		return;
	}

	switch (curr) {
	case BT_HFP_CALL_INCOMING: {
		const char *num = (s_hfp.caller_num[0] != '\0') ? s_hfp.caller_num : "0000000000";
		uint8_t     len = (uint8_t)(strlen(num) + 1);

		bt_hfp_ag_call_incoming(s_hfp.hp_addr, num, len,
								s_hfp.caller_type ? s_hfp.caller_type : 0x81);
		RTK_LOGS(TAG, RTK_LOG_INFO, "relay: phone INCOMING -> ring headphone (%s)\r\n", num);
		break;
	}
	case BT_HFP_CALL_OUTGOING:
		bt_hfp_ag_call_dial(s_hfp.hp_addr);
		bt_hfp_ag_call_alert(s_hfp.hp_addr);
		RTK_LOGS(TAG, RTK_LOG_INFO, "relay: phone OUTGOING -> headphone dialing\r\n");
		break;

	case BT_HFP_CALL_ACTIVE:
	case BT_HFP_CALL_ACTIVE_WITH_CALL_WAITING:
	case BT_HFP_CALL_ACTIVE_WITH_CALL_HELD:
		bt_hfp_ag_call_answer(s_hfp.hp_addr);
		RTK_LOGS(TAG, RTK_LOG_INFO, "relay: phone ACTIVE -> headphone in-call\r\n");
		/* Headphone SCO only after call is ACTIVE (rejected during ringing). */
		hfp_try_bring_up_hp_sco();
		break;

	case BT_HFP_CALL_IDLE:
		if (prev != BT_HFP_CALL_IDLE) {
			bt_hfp_ag_call_terminate(s_hfp.hp_addr);
			RTK_LOGS(TAG, RTK_LOG_INFO, "relay: phone IDLE -> headphone call end\r\n");
		}
		break;

	default:
		break;
	}
}

/* SCO voice: both ends locked CVSD, so packets are raw-forwarded with zero
 * buffering. TX-busy drops a packet (never retry/sleep in the event thread). */
#define HFP_SCO_LOG_PERIOD    200
#define HFP_SCO_PKT_MAX_LEN   255

/* Controller "active SCO": 8761BTV only routes the first SCO of the boot session
 * to HCI. gap_br_vendor_set_active_sco issues HCI vendor opcode 0xFC42 with
 * 4-byte payload [handle LE 2B][activate 1B][policy 1B]. */
#define HFP_SCO_POLICY         0
#define HFP_SCO_RELEASE_ON_DOWN 0
#ifndef HFP_SCO_ACTIVATE_HEADPHONE
#define HFP_SCO_ACTIVATE_HEADPHONE   0
#endif

static void hfp_set_active_sco(uint16_t handle, uint8_t activate, const char *leg)
{
	T_GAP_CAUSE c = gap_br_vendor_set_active_sco(handle, activate, HFP_SCO_POLICY);
	RTK_LOGS(TAG, RTK_LOG_INFO,
		"%s active SCO %s: handle=0x%04x activate=%d policy=%d -> cause %d\r\n",
		activate ? "set" : "release", leg, handle, activate, HFP_SCO_POLICY, (int)c);
}

static uint32_t s_sco_rx_phone, s_sco_rx_hp;
static uint32_t s_sco_to_hp_ok, s_sco_to_hp_drop;
static uint32_t s_sco_to_phone_ok, s_sco_to_phone_drop;
static uint8_t  s_sco_seq_hp, s_sco_seq_phone;

static void hfp_sco_reset_counters(void)
{
	s_sco_rx_phone = s_sco_rx_hp = 0;
	s_sco_to_hp_ok = s_sco_to_hp_drop = 0;
	s_sco_to_phone_ok = s_sco_to_phone_drop = 0;
}

static void hfp_sco_forward(uint8_t *src, uint8_t *data, uint16_t len)
{
	if (data == NULL || len == 0 || len > HFP_SCO_PKT_MAX_LEN) {
		return;
	}
	if (bt_classic_relay_is_headphone(src)) {
		/* headphone mic -> phone */
		if (s_hfp.phone_valid && s_hfp.phone_sco) {
			if (bt_sco_data_send(s_hfp.phone_addr, s_sco_seq_phone++, data, (uint8_t)len)) {
				s_sco_to_phone_ok++;
			} else {
				s_sco_to_phone_drop++;
			}
		}
	} else {
		/* phone downlink -> headphone speaker */
		if (s_hfp.hp_valid && s_hfp.hp_sco) {
			if (bt_sco_data_send(s_hfp.hp_addr, s_sco_seq_hp++, data, (uint8_t)len)) {
				s_sco_to_hp_ok++;
			} else {
				s_sco_to_hp_drop++;
			}
		}
	}
}

void bt_classic_hfp_handle_event(T_BT_EVENT event_type, void *event_buf,
								 uint16_t buf_len)
{
	T_BT_EVENT_PARAM *param = event_buf;

	(void)buf_len;

	switch (event_type) {

	/* ---- phone side: HF role (BT_EVENT_HFP_*) ---- */
	case BT_EVENT_HFP_CONN_IND:
		bt_hfp_connect_cfm(param->hfp_conn_ind.bd_addr, true);
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP conn ind [phone] " BD_FMT ": accept\r\n",
				 BD_ARG(param->hfp_conn_ind.bd_addr));
		break;

	case BT_EVENT_HFP_CONN_CMPL:
		if (bt_classic_relay_is_headphone(param->hfp_conn_cmpl.bd_addr)) {
			break;   /* headphone should use AG role, not HF */
		}
		memcpy(s_hfp.phone_addr, param->hfp_conn_cmpl.bd_addr, 6);
		s_hfp.phone_valid = true;
		s_hfp.phone_call  = BT_HFP_CALL_IDLE;
		RTK_LOGS(TAG, RTK_LOG_INFO, ">>> HFP CONNECTED [phone] " BD_FMT " (hfp=%d) <<<\r\n",
				 BD_ARG(param->hfp_conn_cmpl.bd_addr), param->hfp_conn_cmpl.is_hfp);
		break;

	case BT_EVENT_HFP_DISCONN_CMPL:
		if (s_hfp.phone_valid &&
			memcmp(s_hfp.phone_addr, param->hfp_disconn_cmpl.bd_addr, 6) == 0) {
			s_hfp.phone_valid = false;
			s_hfp.phone_call  = BT_HFP_CALL_IDLE;
			s_hfp.phone_sco   = false;
		}
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP disconn [phone], cause 0x%04x\r\n",
				 param->hfp_disconn_cmpl.cause);
		break;

	case BT_EVENT_HFP_CALL_STATUS: {
		uint8_t prev = param->hfp_call_status.prev_status;
		uint8_t curr = param->hfp_call_status.curr_status;

		s_hfp.phone_call = curr;
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP call status [phone]: %u -> %u\r\n", prev, curr);
		hfp_relay_phone_call_to_hp(prev, curr);
		break;
	}

	case BT_EVENT_HFP_CALLER_ID_IND:
		strncpy(s_hfp.caller_num, param->hfp_caller_id_ind.number,
				sizeof(s_hfp.caller_num) - 1);
		s_hfp.caller_num[sizeof(s_hfp.caller_num) - 1] = '\0';
		s_hfp.caller_type = param->hfp_caller_id_ind.type;
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP caller id [phone]: %s\r\n", s_hfp.caller_num);
		break;

	case BT_EVENT_HFP_RING_ALERT:
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP ring alert [phone] (inband=%d)\r\n",
				 param->hfp_ring_alert.is_inband);
		break;

	/* ---- headphone side: AG role (BT_EVENT_HFP_AG_*) ---- */
	case BT_EVENT_HFP_AG_CONN_IND:
		bt_hfp_ag_connect_cfm(param->hfp_ag_conn_ind.bd_addr, true);
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP AG conn ind [headphone] " BD_FMT ": accept\r\n",
				 BD_ARG(param->hfp_ag_conn_ind.bd_addr));
		break;

	case BT_EVENT_HFP_AG_CONN_CMPL:
		memcpy(s_hfp.hp_addr, param->hfp_ag_conn_cmpl.bd_addr, 6);
		s_hfp.hp_valid = true;
		RTK_LOGS(TAG, RTK_LOG_INFO,
				 ">>> HFP AG CONNECTED [headphone] " BD_FMT " (hfp=%d), call-control relay ready <<<\r\n",
				 BD_ARG(param->hfp_ag_conn_cmpl.bd_addr), param->hfp_ag_conn_cmpl.is_hfp);
		/* If a call is already up, sync the freshly connected headphone. */
		if (s_hfp.phone_call != BT_HFP_CALL_IDLE) {
			hfp_relay_phone_call_to_hp(BT_HFP_CALL_IDLE, s_hfp.phone_call);
		}
		break;

	case BT_EVENT_HFP_AG_DISCONN_CMPL:
		if (s_hfp.hp_valid &&
			memcmp(s_hfp.hp_addr, param->hfp_ag_disconn_cmpl.bd_addr, 6) == 0) {
			s_hfp.hp_valid = false;
			s_hfp.hp_sco   = false;
		}
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP AG disconn [headphone], cause 0x%04x\r\n",
				 param->hfp_ag_disconn_cmpl.cause);
		break;

	case BT_EVENT_HFP_AG_INDICATORS_STATUS_REQ:
		hfp_send_ag_indicators(param->hfp_ag_indicators_status_req.bd_addr);
		RTK_LOGS(TAG, RTK_LOG_INFO, "HFP AG indicators status req [headphone]: replied\r\n");
		break;

	case BT_EVENT_HFP_AG_CALL_ANSWER_REQ:
		/* headphone answer key -> answer phone */
		if (s_hfp.phone_valid) {
			bt_hfp_call_answer_req(s_hfp.phone_addr);
			RTK_LOGS(TAG, RTK_LOG_INFO, "relay: headphone ANSWER -> phone\r\n");
		} else {
			RTK_LOGS(TAG, RTK_LOG_ERROR, "headphone answer but no phone HFP link\r\n");
		}
		break;

	case BT_EVENT_HFP_AG_CALL_TERMINATE_REQ:
		/* headphone hangup key -> hang up phone */
		if (s_hfp.phone_valid) {
			bt_hfp_call_terminate_req(s_hfp.phone_addr);
			RTK_LOGS(TAG, RTK_LOG_INFO, "relay: headphone HANGUP -> phone\r\n");
		} else {
			RTK_LOGS(TAG, RTK_LOG_ERROR, "headphone hangup but no phone HFP link\r\n");
		}
		break;

	/* ---- headphone A2DP connected -> start HFP AG connect ---- */
	case BT_EVENT_A2DP_CONN_CMPL:
		if (bt_classic_relay_is_headphone(param->a2dp_conn_cmpl.bd_addr) && !s_hfp.hp_valid) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"relay: headphone A2DP up -> start HFP AG connect\r\n");
			bt_classic_hfp_connect_headphone(param->a2dp_conn_cmpl.bd_addr);
		}
		break;

	/* ---- SDP: headphone Handsfree service found -> AG connect ---- */
	case BT_EVENT_SDP_ATTR_INFO: {
		T_BT_SDP_ATTR_INFO *info = &param->sdp_attr_info.info;

		if (!bt_classic_relay_is_headphone(param->sdp_attr_info.bd_addr)) {
			break;
		}
		if (info->srv_class_uuid_type == BT_SDP_UUID16 &&
			info->srv_class_uuid_data.uuid_16 == UUID_HANDSFREE) {
			s_hfp.hp_chann = info->server_channel;
			RTK_LOGS(TAG, RTK_LOG_INFO,
					 "relay: headphone Handsfree found (rfc chann %u), AG connect req\r\n",
					 info->server_channel);
			bt_hfp_ag_connect_req(param->sdp_attr_info.bd_addr, info->server_channel, true);
		}
		break;
	}

	/* ---- SCO voice relay ---- */
	case BT_EVENT_SCO_CONN_IND:
		bt_sco_conn_cfm(param->sco_conn_ind.bd_addr, true);
		RTK_LOGS(TAG, RTK_LOG_INFO, "SCO conn ind " BD_FMT ": accept\r\n",
				 BD_ARG(param->sco_conn_ind.bd_addr));
		break;

	case BT_EVENT_SCO_CONN_RSP:
		RTK_LOGS(TAG, RTK_LOG_INFO, "SCO conn rsp %s " BD_FMT ", cause 0x%04x\r\n",
				 bt_classic_relay_is_headphone(param->sco_conn_rsp.bd_addr) ? "[headphone]" : "[phone]",
				 BD_ARG(param->sco_conn_rsp.bd_addr), param->sco_conn_rsp.cause);
		break;

	case BT_EVENT_SCO_CONN_CMPL: {
		uint8_t *addr = param->sco_conn_cmpl.bd_addr;

		if (param->sco_conn_cmpl.cause != 0) {
			RTK_LOGS(TAG, RTK_LOG_ERROR, "SCO conn FAIL " BD_FMT ", cause 0x%04x\r\n",
					 BD_ARG(addr), param->sco_conn_cmpl.cause);
			break;
		}
		if (bt_classic_relay_is_headphone(addr)) {
			s_hfp.hp_sco = true;
			s_hfp.hp_sco_handle = param->sco_conn_cmpl.handle;
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> SCO up [headphone] (handle 0x%04x, air %u, rx %u tx %u) -- voice relay both-way ON <<<\r\n",
				param->sco_conn_cmpl.handle, param->sco_conn_cmpl.air_mode,
				param->sco_conn_cmpl.rx_pkt_len, param->sco_conn_cmpl.tx_pkt_len);
#if HFP_SCO_ACTIVATE_HEADPHONE
			hfp_set_active_sco(s_hfp.hp_sco_handle, 1, "[headphone]");
#endif
		} else {
			s_hfp.phone_sco = true;
			s_hfp.phone_sco_handle = param->sco_conn_cmpl.handle;
			hfp_sco_reset_counters();
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> SCO up [phone] (handle 0x%04x, air %u, rx %u tx %u) <<<\r\n",
				param->sco_conn_cmpl.handle, param->sco_conn_cmpl.air_mode,
				param->sco_conn_cmpl.rx_pkt_len, param->sco_conn_cmpl.tx_pkt_len);
			/* Re-bind active SCO to this call's phone handle so phone RX reaches HCI. */
			hfp_set_active_sco(s_hfp.phone_sco_handle, 1, "[phone]");
			if (!s_hfp.hp_valid) {
				RTK_LOGS(TAG, RTK_LOG_ERROR,
					"relay: hp HFP-AG not connected (hp_valid=0) -> cannot relay call audio to hp\r\n");
			} else if (hfp_call_active(s_hfp.phone_call)) {
				hfp_try_bring_up_hp_sco();
			} else {
				RTK_LOGS(TAG, RTK_LOG_INFO,
					"relay: phone SCO up but call not active yet (call=%u) -> defer headphone SCO to answer\r\n",
					s_hfp.phone_call);
			}
		}
		break;
	}

	case BT_EVENT_SCO_DISCONNECTED: {
		uint8_t *addr  = param->sco_disconnected.bd_addr;
		bool     is_hp = bt_classic_relay_is_headphone(addr);

#if HFP_SCO_RELEASE_ON_DOWN
		hfp_set_active_sco(is_hp ? s_hfp.hp_sco_handle : s_hfp.phone_sco_handle, 0,
						   is_hp ? "[headphone]" : "[phone]");
#endif

		if (is_hp) {
			s_hfp.hp_sco = false;
		} else {
			s_hfp.phone_sco = false;
			/* phone voice ended -> tear down headphone SCO */
			if (s_hfp.hp_valid && s_hfp.hp_sco) {
				bt_hfp_ag_audio_disconnect_req(s_hfp.hp_addr);
			}
		}
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"SCO down %s " BD_FMT ", cause 0x%04x (to_hp ok=%u drop=%u | to_phone ok=%u drop=%u)\r\n",
			is_hp ? "[headphone]" : "[phone]", BD_ARG(addr),
			param->sco_disconnected.cause,
			(unsigned int)s_sco_to_hp_ok, (unsigned int)s_sco_to_hp_drop,
			(unsigned int)s_sco_to_phone_ok, (unsigned int)s_sco_to_phone_drop);
		break;
	}

	case BT_EVENT_SCO_DATA_IND: {
		uint8_t *addr = param->sco_data_ind.bd_addr;

		if (bt_classic_relay_is_headphone(addr)) {
			s_sco_rx_hp++;
		} else {
			s_sco_rx_phone++;
		}
		if (((s_sco_rx_phone + s_sco_rx_hp) % HFP_SCO_LOG_PERIOD) == 1) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"SCO relay: rx phone=%u hp=%u | to_hp ok=%u drop=%u | to_phone ok=%u drop=%u\r\n",
				(unsigned int)s_sco_rx_phone, (unsigned int)s_sco_rx_hp,
				(unsigned int)s_sco_to_hp_ok, (unsigned int)s_sco_to_hp_drop,
				(unsigned int)s_sco_to_phone_ok, (unsigned int)s_sco_to_phone_drop);
		}
		hfp_sco_forward(addr, param->sco_data_ind.p_data, param->sco_data_ind.length);
		break;
	}

	default:
		break;
	}
}

void bt_classic_hfp_connect_headphone(uint8_t *addr)
{
	T_BT_SDP_UUID_DATA uuid;

	memcpy(s_hfp.hp_addr, addr, 6);

	uuid.uuid_16 = UUID_HANDSFREE;
	if (bt_sdp_discov_start(addr, BT_SDP_UUID16, uuid)) {
		RTK_LOGS(TAG, RTK_LOG_INFO,
				 "relay: SDP discov Handsfree to headphone " BD_FMT " started\r\n",
				 BD_ARG(addr));
	} else {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "relay: SDP discov (Handsfree) start failed\r\n");
	}
}

void bt_classic_hfp_init(void)
{
	memset(&s_hfp, 0, sizeof(s_hfp));

	if (!bt_hfp_init(1, BT_CLASSIC_HFP_HF_CHANN, BT_CLASSIC_HSP_HF_CHANN,
					 HFP_HF_FEATURES, HFP_HF_SUPP_CODEC)) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "bt_hfp_init (HF) failed\r\n");
	}
	if (!bt_hfp_ag_init(1, BT_CLASSIC_HFP_AG_CHANN, BT_CLASSIC_HSP_AG_CHANN,
						HFP_AG_FEATURES, HFP_AG_SUPP_CODEC, NULL)) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "bt_hfp_ag_init (AG) failed\r\n");
	}

	RTK_LOGS(TAG, RTK_LOG_INFO,
		"HFP relay init: HF(to phone)+AG(to headphone), locked CVSD, control + SCO voice relay\r\n");
	RTK_LOGS(TAG, RTK_LOG_INFO,
		"HFP CLI: 'bt_call_answer' / 'bt_call_hangup' / 'bt_call' (status)\r\n");
}

/* CLI commands: answer / hangup / status. */

static u32 hfp_cmd_answer(u16 argc, u8 *argv[])
{
	(void)argc; (void)argv;
	if (!s_hfp.phone_valid) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "bt_call_answer: no phone HFP link\r\n");
		return 0;
	}
	bt_hfp_call_answer_req(s_hfp.phone_addr);
	RTK_LOGS(TAG, RTK_LOG_INFO, "bt_call_answer: answer phone\r\n");
	return 1;
}

static u32 hfp_cmd_hangup(u16 argc, u8 *argv[])
{
	(void)argc; (void)argv;
	if (!s_hfp.phone_valid) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "bt_call_hangup: no phone HFP link\r\n");
		return 0;
	}
	bt_hfp_call_terminate_req(s_hfp.phone_addr);
	RTK_LOGS(TAG, RTK_LOG_INFO, "bt_call_hangup: terminate phone call\r\n");
	return 1;
}

static u32 hfp_cmd_status(u16 argc, u8 *argv[])
{
	(void)argc; (void)argv;
	RTK_LOGS(TAG, RTK_LOG_INFO,
		"hfp status: phone conn=%d call=%u  | headphone(AG) conn=%d chann=%u\r\n",
		s_hfp.phone_valid, s_hfp.phone_call, s_hfp.hp_valid, s_hfp.hp_chann);
	if (s_hfp.phone_valid) {
		RTK_LOGS(TAG, RTK_LOG_INFO, "  phone " BD_FMT "\r\n", BD_ARG(s_hfp.phone_addr));
	}
	if (s_hfp.hp_valid) {
		RTK_LOGS(TAG, RTK_LOG_INFO, "  headphone " BD_FMT "\r\n", BD_ARG(s_hfp.hp_addr));
	}
	return 1;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE bt_hfp_answer_cmd[] = {
	{"bt_call_answer", hfp_cmd_answer},
};
CMD_TABLE_DATA_SECTION
const COMMAND_TABLE bt_hfp_hangup_cmd[] = {
	{"bt_call_hangup", hfp_cmd_hangup},
};
CMD_TABLE_DATA_SECTION
const COMMAND_TABLE bt_hfp_status_cmd[] = {
	{"bt_call", hfp_cmd_status},
};

#endif /* BT_CLASSIC_ENABLE_HFP */

#endif /* CONFIG_BT_EXT */
