/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth A2DP relay (Sink for phone + Source for headphone).
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>
#include <stdbool.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "btm.h"
#include "bt_a2dp.h"
#include "bt_avrcp.h"
#include "bt_sdp.h"
#include "gap_br.h"
#include "bt_types.h"

#include "bt_classic_a2dp.h"
#include "bt_classic_link.h"

static const char *const TAG = "BTEXT_A2DP";

/* Locked SBC config: Sink and Source endpoints must match so raw SBC frames relay. */
#define RELAY_SBC_FREQ_MASK    BT_A2DP_SBC_SAMPLING_FREQUENCY_44_1KHZ
#define RELAY_SBC_CHAN_MASK    BT_A2DP_SBC_CHANNEL_MODE_JOINT_STEREO
#define RELAY_SBC_BLOCK_MASK   BT_A2DP_SBC_BLOCK_LENGTH_16
#define RELAY_SBC_SUB_MASK     BT_A2DP_SBC_SUBBANDS_8
#define RELAY_SBC_ALLOC_MASK   BT_A2DP_SBC_ALLOCATION_METHOD_LOUDNESS
#define RELAY_SBC_MIN_BITPOOL  2
/* max_bitpool caps SBC bitrate; lower it (e.g. 35) if double-hop relay drops frames. */
#define RELAY_SBC_MAX_BITPOOL  53

#define RELAY_A2DP_LINK_NUM    2
#define RELAY_A2DP_LATENCY     180

/* Throttle RX media-packet logging (~50 packets per line, ~1 line/sec). */
#define RELAY_A2DP_RX_LOG_PERIOD   50

/* Headphone (A2DP Source side) relay state. */
static struct {
	bool    addr_valid;
	uint8_t addr[6];
	bool    connected;
	bool    stream_open;
	bool    streaming;
} s_hp;

static uint32_t s_rx_count;
static uint32_t s_fwd_ok;
static uint32_t s_fwd_drop;

static bool relay_is_headphone(uint8_t *addr)
{
	return s_hp.addr_valid && memcmp(s_hp.addr, addr, 6) == 0;
}

bool bt_classic_relay_is_headphone(uint8_t *addr)
{
	return relay_is_headphone(addr);
}

bool bt_classic_relay_get_headphone(uint8_t out[6])
{
	if (s_hp.addr_valid && s_hp.connected) {
		memcpy(out, s_hp.addr, 6);
		return true;
	}
	return false;
}

/* Forward FIFO: absorbs transient TX congestion. RX/RSP events drive draining;
 * no timer is used (the relay has no clock of its own). Drained on the single BT
 * event thread, so no locking needed. When full, drop oldest to cap latency. */
#define RELAY_FIFO_DEPTH    16            /* must be a power of two */
#define RELAY_FIFO_MASK     (RELAY_FIFO_DEPTH - 1)
#define RELAY_PKT_MAX_LEN   700

typedef struct {
	uint16_t len;
	uint16_t seq_num;
	uint32_t timestamp;
	uint8_t  frame_num;
	uint8_t  data[RELAY_PKT_MAX_LEN];
} relay_pkt_t;

static relay_pkt_t s_fifo[RELAY_FIFO_DEPTH];
static uint16_t    s_fifo_head;
static uint16_t    s_fifo_tail;
static uint16_t    s_fifo_peak;

static void relay_fifo_reset(void)
{
	s_fifo_head = s_fifo_tail = s_fifo_peak = 0;
}

static void relay_fifo_drain(void)
{
	while (s_fifo_tail != s_fifo_head) {
		relay_pkt_t *p = &s_fifo[s_fifo_tail & RELAY_FIFO_MASK];

		if (!bt_a2dp_stream_data_send(s_hp.addr, p->seq_num, p->timestamp,
									  p->frame_num, p->data, p->len,
									  true /* flushable */)) {
			break;
		}
		s_fwd_ok++;
		s_fifo_tail++;
	}
}

static void relay_fifo_push(uint16_t seq_num, uint32_t timestamp,
							uint8_t frame_num, uint8_t *payload, uint16_t len)
{
	relay_pkt_t *p;
	uint16_t     used;

	if (len > RELAY_PKT_MAX_LEN) {
		s_fwd_drop++;
		return;
	}
	if ((uint16_t)(s_fifo_head - s_fifo_tail) >= RELAY_FIFO_DEPTH) {
		s_fifo_tail++;   /* full: drop oldest to cap latency */
		s_fwd_drop++;
	}
	p = &s_fifo[s_fifo_head & RELAY_FIFO_MASK];
	p->len       = len;
	p->seq_num   = seq_num;
	p->timestamp = timestamp;
	p->frame_num = frame_num;
	memcpy(p->data, payload, len);
	s_fifo_head++;

	used = (uint16_t)(s_fifo_head - s_fifo_tail);
	if (used > s_fifo_peak) { s_fifo_peak = used; }

	relay_fifo_drain();
}

/* Sync headphone source stream to phone play state (idempotent). */
void bt_classic_relay_set_play_state(bool playing)
{
	if (!s_hp.connected || !s_hp.stream_open) {
		return;
	}
	if (playing) {
		if (!s_hp.streaming) {
			bt_a2dp_stream_start_req(s_hp.addr);
		}
	} else if (s_hp.streaming) {
		bt_a2dp_stream_suspend_req(s_hp.addr);
		s_hp.streaming = false;
		relay_fifo_reset();
	}
}

static void relay_add_sbc_sep(T_BT_A2DP_ROLE role)
{
	T_BT_A2DP_STREAM_ENDPOINT sep;

	memset(&sep, 0, sizeof(sep));
	sep.role = role;
	sep.codec_type = BT_A2DP_CODEC_TYPE_SBC;
	sep.u.codec_sbc.sampling_frequency_mask = RELAY_SBC_FREQ_MASK;
	sep.u.codec_sbc.channel_mode_mask       = RELAY_SBC_CHAN_MASK;
	sep.u.codec_sbc.block_length_mask       = RELAY_SBC_BLOCK_MASK;
	sep.u.codec_sbc.subbands_mask           = RELAY_SBC_SUB_MASK;
	sep.u.codec_sbc.allocation_method_mask  = RELAY_SBC_ALLOC_MASK;
	sep.u.codec_sbc.min_bitpool             = RELAY_SBC_MIN_BITPOOL;
	sep.u.codec_sbc.max_bitpool             = RELAY_SBC_MAX_BITPOOL;
	bt_a2dp_stream_endpoint_add(sep);
}

static void relay_scan(void)
{
	T_GAP_CAUSE cause = gap_br_start_inquiry(false, 8);

	RTK_LOGS(TAG, RTK_LOG_INFO, "relay: start inquiry, cause 0x%02x\r\n", cause);
}

static void relay_connect(uint8_t *addr)
{
	T_BT_SDP_UUID_DATA uuid;

	memset(&s_hp, 0, sizeof(s_hp));
	memcpy(s_hp.addr, addr, 6);
	s_hp.addr_valid = true;

	gap_br_stop_inquiry();

	uuid.uuid_16 = UUID_AUDIO_SINK;
	if (bt_sdp_discov_start(addr, BT_SDP_UUID16, uuid)) {
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"relay: SDP discov to headphone " BD_FMT " started\r\n", BD_ARG(addr));
	} else {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "relay: SDP discov start failed\r\n");
	}
}

void bt_classic_a2dp_handle_event(T_BT_EVENT event_type, void *event_buf,
								   uint16_t buf_len)
{
	T_BT_EVENT_PARAM    *param = event_buf;
	T_BT_CLASSIC_BR_LINK *p_link;

	(void)buf_len;

	switch (event_type) {

	case BT_EVENT_INQUIRY_RESULT:
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"scan: " BD_FMT "  cod 0x%06x  rssi %d  name \"%s\"\r\n",
			BD_ARG(param->inquiry_result.bd_addr),
			(unsigned int)param->inquiry_result.cod,
			param->inquiry_result.rssi,
			param->inquiry_result.name);
		break;

	case BT_EVENT_INQUIRY_CMPL:
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"scan done. use: bt_connect <addr>  e.g. bt_connect 11:22:33:44:55:66\r\n");
		break;

	case BT_EVENT_SDP_ATTR_INFO: {
		T_BT_SDP_ATTR_INFO *info = &param->sdp_attr_info.info;

		if (!relay_is_headphone(param->sdp_attr_info.bd_addr)) {
			break;
		}
		if (info->srv_class_uuid_type == BT_SDP_UUID16 &&
			info->srv_class_uuid_data.uuid_16 == UUID_AUDIO_SINK) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"relay: headphone Audio Sink found, A2DP connect req (ver 0x%04x)\r\n",
				info->protocol_version);
			bt_a2dp_connect_req(param->sdp_attr_info.bd_addr,
								info->protocol_version, BT_A2DP_ROLE_SNK);
		}
		break;
	}

	case BT_EVENT_SDP_DISCOV_CMPL:
		RTK_LOGS(TAG, RTK_LOG_INFO,
				 "relay: SDP discov cmpl " BD_FMT ", cause 0x%04x (l2c free=%d)\r\n",
				 BD_ARG(param->sdp_discov_cmpl.bd_addr),
				 param->sdp_discov_cmpl.cause,
				 l2c_get_free_chann_num(0));
		break;

	case BT_EVENT_A2DP_CONN_IND: {
		uint8_t *addr = param->a2dp_conn_ind.bd_addr;
		bool     ok   = bt_a2dp_connect_cfm(addr, true);

		RTK_LOGS(TAG, RTK_LOG_INFO, "A2DP conn ind " BD_FMT ": accept cfm=%d\r\n",
				 BD_ARG(addr), ok);
		break;
	}

	case BT_EVENT_A2DP_CONN_CMPL:
		if (relay_is_headphone(param->a2dp_conn_cmpl.bd_addr)) {
			s_hp.connected = true;
			/* Also open AVRCP to headphone to relay phone volume commands. */
			bool avrcp = bt_avrcp_connect_req(s_hp.addr);
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> A2DP CONN CMPL [headphone] " BD_FMT " <<< (avrcp req=%d)\r\n",
				BD_ARG(param->a2dp_conn_cmpl.bd_addr), avrcp);
		} else {
			p_link = bt_classic_find_br_link(param->a2dp_conn_cmpl.bd_addr);
			if (p_link != NULL) {
				p_link->connected_profile |= BT_CLASSIC_A2DP_PROFILE_MASK;
			}
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> A2DP CONN CMPL [phone] " BD_FMT " <<<\r\n",
				BD_ARG(param->a2dp_conn_cmpl.bd_addr));
		}
		break;

	case BT_EVENT_A2DP_CONN_FAIL:
		RTK_LOGS(TAG, RTK_LOG_ERROR,
			">>> A2DP CONN FAIL " BD_FMT ", cause 0x%04x (l2c free=%d) <<<\r\n",
			BD_ARG(param->a2dp_conn_fail.bd_addr), param->a2dp_conn_fail.cause,
			l2c_get_free_chann_num(0));
		break;

	case BT_EVENT_A2DP_DISCONN_CMPL:
		if (relay_is_headphone(param->a2dp_disconn_cmpl.bd_addr)) {
			s_hp.connected = s_hp.stream_open = s_hp.streaming = false;
			relay_fifo_reset();
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"A2DP disconn [headphone] " BD_FMT ", cause 0x%04x\r\n",
				BD_ARG(param->a2dp_disconn_cmpl.bd_addr),
				param->a2dp_disconn_cmpl.cause);
		} else {
			p_link = bt_classic_find_br_link(param->a2dp_disconn_cmpl.bd_addr);
			if (p_link != NULL) {
				p_link->connected_profile &= ~BT_CLASSIC_A2DP_PROFILE_MASK;
				p_link->streaming_fg = false;
			}
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"A2DP disconn [phone] " BD_FMT ", cause 0x%04x\r\n",
				BD_ARG(param->a2dp_disconn_cmpl.bd_addr),
				param->a2dp_disconn_cmpl.cause);
		}
		break;

	case BT_EVENT_A2DP_CONFIG_CMPL:
		p_link = bt_classic_find_br_link(param->a2dp_config_cmpl.bd_addr);
		if (p_link != NULL && param->a2dp_config_cmpl.codec_type == BT_A2DP_CODEC_TYPE_SBC) {
			p_link->a2dp_codec_type = BT_A2DP_CODEC_TYPE_SBC;
			p_link->a2dp_codec_info.sbc.sampling_frequency =
				param->a2dp_config_cmpl.codec_info.sbc.sampling_frequency;
			p_link->a2dp_codec_info.sbc.channel_mode =
				param->a2dp_config_cmpl.codec_info.sbc.channel_mode;
			p_link->a2dp_codec_info.sbc.block_length =
				param->a2dp_config_cmpl.codec_info.sbc.block_length;
			p_link->a2dp_codec_info.sbc.subbands =
				param->a2dp_config_cmpl.codec_info.sbc.subbands;
			p_link->a2dp_codec_info.sbc.allocation_method =
				param->a2dp_config_cmpl.codec_info.sbc.allocation_method;
			p_link->a2dp_codec_info.sbc.min_bitpool =
				param->a2dp_config_cmpl.codec_info.sbc.min_bitpool;
			p_link->a2dp_codec_info.sbc.max_bitpool =
				param->a2dp_config_cmpl.codec_info.sbc.max_bitpool;
		}
		{
			const char *tag = relay_is_headphone(param->a2dp_config_cmpl.bd_addr)
						? "[headphone]" : "[phone]";
			static const char *freq_str[] = {"16k","32k","44.1k","48k"};
			static const uint8_t freq_bits[] = {7,6,5,4};
			uint8_t f = param->a2dp_config_cmpl.codec_info.sbc.sampling_frequency;
			const char *sf = "?";
			for (int i = 0; i < 4; i++) {
				if (f & (1 << freq_bits[i])) { sf = freq_str[i]; break; }
			}
			static const char *chan_str[] = {"MONO","DUAL","STEREO","JOINT"};
			uint8_t cm = param->a2dp_config_cmpl.codec_info.sbc.channel_mode;
			const char *ch = "?";
			for (int i = 0; i < 4; i++) {
				if (cm & (1 << (3 - i))) { ch = chan_str[i]; break; }
			}
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> A2DP config %s: SBC %s/%s/blk%d/sub%d/%s "
				"bitpool=%d..%d <<<\r\n",
				tag, sf, ch,
				(param->a2dp_config_cmpl.codec_info.sbc.block_length & 0xF0) ? 16 :
				(param->a2dp_config_cmpl.codec_info.sbc.block_length & 0x20) ? 12 : 8,
				(param->a2dp_config_cmpl.codec_info.sbc.subbands & 4) ? 8 : 4,
				(param->a2dp_config_cmpl.codec_info.sbc.allocation_method & 2) ? "SNR" : "LOUD",
				param->a2dp_config_cmpl.codec_info.sbc.min_bitpool,
				param->a2dp_config_cmpl.codec_info.sbc.max_bitpool);
		}
		break;

	case BT_EVENT_A2DP_STREAM_OPEN:
		if (relay_is_headphone(param->a2dp_stream_open.bd_addr)) {
			s_hp.stream_open = true;
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> A2DP STREAM OPEN [headphone] (max_pkt %d), start src stream <<<\r\n",
				param->a2dp_stream_open.max_pkt_len);
			bt_a2dp_stream_start_req(s_hp.addr);
		} else {
			RTK_LOGS(TAG, RTK_LOG_INFO, ">>> A2DP STREAM OPEN [phone] <<<\r\n");
		}
		break;

	case BT_EVENT_A2DP_STREAM_OPEN_FAIL:
		RTK_LOGS(TAG, RTK_LOG_ERROR,
			">>> A2DP STREAM OPEN FAIL " BD_FMT ", cause 0x%04x (l2c free=%d) <<<\r\n",
			BD_ARG(param->a2dp_stream_open_fail.bd_addr),
			param->a2dp_stream_open_fail.cause,
			l2c_get_free_chann_num(0));
		break;

	case BT_EVENT_A2DP_STREAM_START_IND: {
		uint8_t *addr = param->a2dp_stream_start_ind.bd_addr;

		p_link = bt_classic_find_br_link(addr);
		if (p_link != NULL) {
			p_link->streaming_fg = true;
		}
		bt_a2dp_stream_start_cfm(addr, true);
		s_rx_count = s_fwd_ok = s_fwd_drop = 0;
		relay_fifo_reset();

		/* Phone resumed: restart the headphone source stream so forwarding works. */
		if (!relay_is_headphone(addr) &&
			s_hp.connected && s_hp.stream_open && !s_hp.streaming) {
			bt_a2dp_stream_start_req(s_hp.addr);
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"relay: phone resumed -> restart headphone src stream\r\n");
		}
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"A2DP stream start [phone]: accepted (hp_streaming=%d)\r\n", s_hp.streaming);
		break;
	}

	case BT_EVENT_A2DP_STREAM_START_RSP:
		if (relay_is_headphone(param->a2dp_stream_start_rsp.bd_addr)) {
			s_hp.streaming = true;
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> A2DP src stream STARTED [headphone], forwarding enabled <<<\r\n");
		}
		break;

	case BT_EVENT_A2DP_STREAM_DATA_IND: {
		uint16_t len = param->a2dp_stream_data_ind.len;

		s_rx_count++;
		if (s_rx_count == 1 || (s_rx_count % RELAY_A2DP_RX_LOG_PERIOD) == 0) {
			RTK_LOGS(TAG, RTK_LOG_INFO,
				"A2DP RX [phone] #%u: len=%u seq=%u frames=%u "
				"(hp_streaming=%d fwd ok=%u drop=%u fifo=%u/%u peak=%u)\r\n",
				(unsigned int)s_rx_count, (unsigned int)len,
				(unsigned int)param->a2dp_stream_data_ind.seq_num,
				(unsigned int)param->a2dp_stream_data_ind.frame_num,
				s_hp.streaming, (unsigned int)s_fwd_ok, (unsigned int)s_fwd_drop,
				(unsigned int)(uint16_t)(s_fifo_head - s_fifo_tail),
				(unsigned int)RELAY_FIFO_DEPTH, (unsigned int)s_fifo_peak);
		}

		if (s_hp.streaming) {
			relay_fifo_push(param->a2dp_stream_data_ind.seq_num,
							param->a2dp_stream_data_ind.timestamp,
							param->a2dp_stream_data_ind.frame_num,
							param->a2dp_stream_data_ind.payload, len);
		}
		break;
	}

	case BT_EVENT_A2DP_STREAM_STOP:
	case BT_EVENT_A2DP_STREAM_CLOSE: {
		uint8_t *addr = (event_type == BT_EVENT_A2DP_STREAM_STOP)
						? param->a2dp_stream_stop.bd_addr
						: param->a2dp_stream_close.bd_addr;

		if (!relay_is_headphone(addr)) {
			/* Phone stopped/closed: also suspend the headphone source stream. */
			p_link = bt_classic_find_br_link(addr);
			if (p_link != NULL) {
				p_link->streaming_fg = false;
			}
			if (s_hp.streaming) {
				bt_a2dp_stream_suspend_req(s_hp.addr);
				s_hp.streaming = false;
			}
			relay_fifo_reset();
		} else {
			/* CLOSE needs a fresh OPEN, so clear stream_open to avoid starting a closed stream. */
			s_hp.streaming = false;
			if (event_type == BT_EVENT_A2DP_STREAM_CLOSE) {
				s_hp.stream_open = false;
			}
		}
		RTK_LOGS(TAG, RTK_LOG_INFO, "A2DP stream %s %s (fwd ok=%u drop=%u)\r\n",
				 (event_type == BT_EVENT_A2DP_STREAM_STOP) ? "stop" : "close",
				 relay_is_headphone(addr) ? "[headphone]" : "[phone]",
				 (unsigned int)s_fwd_ok, (unsigned int)s_fwd_drop);
		break;
	}

	case BT_EVENT_A2DP_STREAM_DATA_RSP:
		/* Each DATA_RSP frees TX-queue space; use it to drain the FIFO (no logging: ~tens/sec). */
		relay_fifo_drain();
		break;

	default:
		if ((event_type & 0xFF00) == 0x3100) {
			RTK_LOGS(TAG, RTK_LOG_INFO, "a2dp event 0x%04x\r\n", event_type);
		}
		break;
	}
}

void bt_classic_a2dp_init(void)
{
	memset(&s_hp, 0, sizeof(s_hp));
	s_rx_count = s_fwd_ok = s_fwd_drop = 0;
	relay_fifo_reset();

	bt_a2dp_init(RELAY_A2DP_LINK_NUM, RELAY_A2DP_LATENCY,
				 BT_A2DP_CAPABILITY_MEDIA_CODEC | BT_A2DP_CAPABILITY_MEDIA_TRANSPORT);

	/* Both endpoints use the same locked SBC config: SNK for phone, SRC for headphone. */
	relay_add_sbc_sep(BT_A2DP_ROLE_SNK);
	relay_add_sbc_sep(BT_A2DP_ROLE_SRC);

	RTK_LOGS(TAG, RTK_LOG_INFO,
		"A2DP relay init: sink+source, locked SBC 44.1k/joint/blk16/8sub/loudness\r\n");
	RTK_LOGS(TAG, RTK_LOG_INFO,
		"relay CLI: 'bt_scan' then 'bt_connect <addr>' to bridge to a headphone\r\n");
}

static int relay_hexval(char c)
{
	if (c >= '0' && c <= '9') { return c - '0'; }
	if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
	if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
	return -1;
}

/* Parse "AA:BB:CC:DD:EE:FF" (display order, MSB first) into out[5..0]. */
static bool relay_parse_addr(const char *s, uint8_t out[6])
{
	uint8_t b[6];
	int     bi = 0, hi = 0, cur = 0, v;

	for (; *s && bi < 6; s++) {
		if (*s == ':' || *s == '-' || *s == ' ') { continue; }
		v = relay_hexval(*s);
		if (v < 0) { return false; }
		cur = (cur << 4) | v;
		if (++hi == 2) { b[bi++] = (uint8_t)cur; cur = 0; hi = 0; }
	}
	if (bi != 6 || hi != 0) { return false; }
	for (bi = 0; bi < 6; bi++) { out[5 - bi] = b[bi]; }
	return true;
}

static u32 relay_cmd_scan(u16 argc, u8 *argv[])
{
	(void)argc; (void)argv;
	relay_scan();
	return 1;
}

static u32 relay_cmd_connect(u16 argc, u8 *argv[])
{
	uint8_t addr[6];

	if (argc < 1 || !relay_parse_addr((const char *)argv[0], addr)) {
		RTK_LOGS(TAG, RTK_LOG_ERROR,
			"usage: bt_connect <addr>  e.g. bt_connect 11:22:33:44:55:66\r\n");
		return 0;
	}
	relay_connect(addr);
	return 1;
}

static u32 relay_cmd_status(u16 argc, u8 *argv[])
{
	(void)argc; (void)argv;
	if (s_hp.addr_valid) {
		RTK_LOGS(TAG, RTK_LOG_INFO,
			"relay status: hp " BD_FMT " conn=%d open=%d streaming=%d  "
			"fwd ok=%u drop=%u  fifo=%u/%u peak=%u\r\n",
			BD_ARG(s_hp.addr), s_hp.connected, s_hp.stream_open, s_hp.streaming,
			(unsigned int)s_fwd_ok, (unsigned int)s_fwd_drop,
			(unsigned int)(uint16_t)(s_fifo_head - s_fifo_tail),
			(unsigned int)RELAY_FIFO_DEPTH, (unsigned int)s_fifo_peak);
	} else {
		RTK_LOGS(TAG, RTK_LOG_INFO, "relay status: no headphone set (run bt_scan/bt_connect)\r\n");
	}
	return 1;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE bt_relay_scan_cmd[] = {
	{"bt_scan", relay_cmd_scan},
};
CMD_TABLE_DATA_SECTION
const COMMAND_TABLE bt_relay_connect_cmd[] = {
	{"bt_connect", relay_cmd_connect},
};
CMD_TABLE_DATA_SECTION
const COMMAND_TABLE bt_relay_status_cmd[] = {
	{"bt_relay", relay_cmd_status},
};

#endif /* CONFIG_BT_EXT */