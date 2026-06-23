/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth AVRCP implementation (bluetooth_ext / external RTL8761B).
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>

#include "ameba_soc.h"
#include "btm.h"
#include "bt_avrcp.h"

#include "bt_classic_avrcp.h"
#include "bt_classic_link.h"
#include "bt_classic_a2dp.h"

static const char *const TAG = "BTEXT_AVRC";

#define BT_CLASSIC_VOL_MAX     0x7f
#define BT_CLASSIC_VOL_MIN     0x00

/* Authoritative volume used to dedup phone<->headphone sync (drops echoes). */
static uint8_t s_curr_volume = 0x3f;

static bool s_hp_avrcp_ready;

static uint8_t s_phone_addr[6];
static bool    s_phone_valid;
static bool    s_phone_vol_reg;

/* Must stay silent: runs on BT event thread, can be triggered at high rate. */
static void relay_volume_to_headphone(uint8_t vol)
{
	uint8_t hp_addr[6];

	if (s_hp_avrcp_ready && bt_classic_relay_get_headphone(hp_addr)) {
		bt_avrcp_absolute_volume_set(hp_addr, vol);
	}
}

static void relay_volume_to_phone(uint8_t vol)
{
	if (s_phone_valid && s_phone_vol_reg) {
		bt_avrcp_volume_change_req(s_phone_addr, vol);
	}
}

static void relay_passthrough_to_phone(uint8_t *src, T_BT_EVENT ev)
{
	if (!bt_classic_relay_is_headphone(src) || !s_phone_valid) {
		return;
	}
	switch (ev) {
	case BT_EVENT_AVRCP_PLAY:     bt_avrcp_play(s_phone_addr);     break;
	case BT_EVENT_AVRCP_PAUSE:    bt_avrcp_pause(s_phone_addr);    break;
	case BT_EVENT_AVRCP_STOP:     bt_avrcp_stop(s_phone_addr);     break;
	case BT_EVENT_AVRCP_FORWARD:  bt_avrcp_forward(s_phone_addr);  break;
	case BT_EVENT_AVRCP_BACKWARD: bt_avrcp_backward(s_phone_addr); break;
	default: return;
	}
	RTK_LOGS(TAG, RTK_LOG_INFO, "AVRCP passthrough headphone->phone: 0x%04x\r\n", ev);
}

void bt_classic_avrcp_handle_event(T_BT_EVENT event_type, void *event_buf,
								   uint16_t buf_len)
{
	T_BT_EVENT_PARAM    *param = event_buf;
	T_BT_CLASSIC_BR_LINK *p_link;

	(void)buf_len;

	switch (event_type) {
	case BT_EVENT_AVRCP_CONN_IND:
		/* Headphone link may not be in br_link table, so accept it by address too. */
		p_link = bt_classic_find_br_link(param->avrcp_conn_ind.bd_addr);
		if (p_link != NULL ||
			bt_classic_relay_is_headphone(param->avrcp_conn_ind.bd_addr)) {
			bt_avrcp_connect_cfm(param->avrcp_conn_ind.bd_addr, true);
			RTK_LOGS(TAG, RTK_LOG_INFO, "AVRCP conn ind: accepted\r\n");
		}
		break;

	case BT_EVENT_AVRCP_CONN_CMPL:
		if (bt_classic_relay_is_headphone(param->avrcp_conn_cmpl.bd_addr)) {
			s_hp_avrcp_ready = true;
			relay_volume_to_headphone(s_curr_volume);
			RTK_LOGS(TAG, RTK_LOG_INFO,
				">>> AVRCP CONNECTED [headphone], control relay ready <<<\r\n");
		} else {
			p_link = bt_classic_find_br_link(param->avrcp_conn_cmpl.bd_addr);
			if (p_link != NULL) {
				p_link->connected_profile |= BT_CLASSIC_AVRCP_PROFILE_MASK;
			}
			memcpy(s_phone_addr, param->avrcp_conn_cmpl.bd_addr, 6);
			s_phone_valid = true;
			RTK_LOGS(TAG, RTK_LOG_INFO, ">>> AVRCP CONNECTED [phone] <<<\r\n");
		}
		break;

	case BT_EVENT_AVRCP_DISCONN_CMPL:
		if (bt_classic_relay_is_headphone(param->avrcp_disconn_cmpl.bd_addr)) {
			s_hp_avrcp_ready = false;
			RTK_LOGS(TAG, RTK_LOG_INFO, "AVRCP disconn [headphone], cause 0x%04x\r\n",
					 param->avrcp_disconn_cmpl.cause);
		} else {
			p_link = bt_classic_find_br_link(param->avrcp_disconn_cmpl.bd_addr);
			if (p_link != NULL) {
				p_link->connected_profile &= ~BT_CLASSIC_AVRCP_PROFILE_MASK;
			}
			if (s_phone_valid &&
				memcmp(s_phone_addr, param->avrcp_disconn_cmpl.bd_addr, 6) == 0) {
				s_phone_valid = false;
				s_phone_vol_reg = false;
			}
			RTK_LOGS(TAG, RTK_LOG_INFO, "AVRCP disconn [phone], cause 0x%04x\r\n",
					 param->avrcp_disconn_cmpl.cause);
		}
		break;

	case BT_EVENT_AVRCP_ABSOLUTE_VOLUME_SET:
		if (bt_classic_relay_is_headphone(param->avrcp_absolute_volume_set.bd_addr)) {
			break;
		}
		if (param->avrcp_absolute_volume_set.volume != s_curr_volume) {
			s_curr_volume = param->avrcp_absolute_volume_set.volume;
			relay_volume_to_headphone(s_curr_volume);
		}
		break;

	case BT_EVENT_AVRCP_VOLUME_CHANGED:
		if (bt_classic_relay_is_headphone(param->avrcp_volume_changed.bd_addr)) {
			if (param->avrcp_volume_changed.volume != s_curr_volume) {
				s_curr_volume = param->avrcp_volume_changed.volume;
				relay_volume_to_phone(s_curr_volume);
			}
		} else {
			s_curr_volume = param->avrcp_volume_changed.volume;
		}
		break;

	case BT_EVENT_AVRCP_KEY_VOLUME_UP:
		if (s_curr_volume < BT_CLASSIC_VOL_MAX) {
			s_curr_volume++;
		}
		if (bt_classic_relay_is_headphone(param->avrcp_key_volume_up.bd_addr)) {
			relay_volume_to_phone(s_curr_volume);
		} else {
			relay_volume_to_headphone(s_curr_volume);
		}
		break;

	case BT_EVENT_AVRCP_KEY_VOLUME_DOWN:
		if (s_curr_volume > BT_CLASSIC_VOL_MIN) {
			s_curr_volume--;
		}
		if (bt_classic_relay_is_headphone(param->avrcp_key_volume_down.bd_addr)) {
			relay_volume_to_phone(s_curr_volume);
		} else {
			relay_volume_to_headphone(s_curr_volume);
		}
		break;

	case BT_EVENT_AVRCP_REG_VOLUME_CHANGED:
		if (bt_classic_relay_is_headphone(param->avrcp_reg_volume_changed.bd_addr)) {
			break;
		}
		bt_avrcp_volume_change_register_rsp(param->avrcp_reg_volume_changed.bd_addr,
											s_curr_volume);
		memcpy(s_phone_addr, param->avrcp_reg_volume_changed.bd_addr, 6);
		s_phone_valid = true;
		s_phone_vol_reg = true;
		RTK_LOGS(TAG, RTK_LOG_INFO, "AVRCP reg volume changed, rsp %d\r\n",
				 s_curr_volume);
		break;

	case BT_EVENT_AVRCP_PLAY_STATUS_CHANGED:
		p_link = bt_classic_find_br_link(param->avrcp_play_status_changed.bd_addr);
		if (p_link != NULL) {
			p_link->avrcp_play_status =
				param->avrcp_play_status_changed.play_status;
		}
		/* Mirror phone play/pause onto headphone stream to keep states in sync. */
		if (!bt_classic_relay_is_headphone(param->avrcp_play_status_changed.bd_addr)) {
			bt_classic_relay_set_play_state(
				param->avrcp_play_status_changed.play_status ==
				BT_AVRCP_PLAY_STATUS_PLAYING);
		}
		RTK_LOGS(TAG, RTK_LOG_INFO, "AVRCP play status: %d\r\n",
				 param->avrcp_play_status_changed.play_status);
		break;

	case BT_EVENT_AVRCP_PLAY:
		relay_passthrough_to_phone(param->avrcp_play.bd_addr, event_type);
		break;
	case BT_EVENT_AVRCP_PAUSE:
		relay_passthrough_to_phone(param->avrcp_pause.bd_addr, event_type);
		break;
	case BT_EVENT_AVRCP_STOP:
		relay_passthrough_to_phone(param->avrcp_stop.bd_addr, event_type);
		break;
	case BT_EVENT_AVRCP_FORWARD:
		relay_passthrough_to_phone(param->avrcp_forward.bd_addr, event_type);
		break;
	case BT_EVENT_AVRCP_BACKWARD:
		relay_passthrough_to_phone(param->avrcp_backward.bd_addr, event_type);
		break;

	default:
		/* 0x32xx notifications can arrive at high rate; keep at DEBUG to avoid stalling A2DP. */
		if ((event_type & 0xFF00) == 0x3200) {
			RTK_LOGS(TAG, RTK_LOG_DEBUG, "avrcp event 0x%04x\r\n", event_type);
		}
		break;
	}
}

void bt_classic_avrcp_init(void)
{
	s_hp_avrcp_ready = false;
	s_phone_valid = false;
	s_phone_vol_reg = false;
	bt_avrcp_init(2);

	/* Cat1 = Player/Recorder (play control), Cat2 = Monitor/Amplifier (abs volume).
	 * supported_features is global (same for phone and headphone); declaring TG=Cat1
	 * makes the phone poll status on the audio ACL and may cause audio glitches. */
	bt_avrcp_supported_features_set(
		BT_AVRCP_FEATURE_CATEGORY_1 | BT_AVRCP_FEATURE_CATEGORY_2,   /* CT */
		BT_AVRCP_FEATURE_CATEGORY_1 | BT_AVRCP_FEATURE_CATEGORY_2);  /* TG */

	RTK_LOGS(TAG, RTK_LOG_INFO, "AVRCP init (CT=Cat1|2, TG=Cat1|2; hp keys->phone on)\r\n");
}

#endif /* CONFIG_BT_EXT */
