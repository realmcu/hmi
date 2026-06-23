/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth (BR/EDR) link table, keyed by bd_addr.
 */

#ifndef __BT_CLASSIC_LINK_H__
#define __BT_CLASSIC_LINK_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BD address print helpers (printed reversed: AA:BB:..:FF). */
#define BD_FMT          "%02x:%02x:%02x:%02x:%02x:%02x"
#define BD_ARG(a)       (a)[5], (a)[4], (a)[3], (a)[2], (a)[1], (a)[0]

extern unsigned char l2c_get_free_chann_num(unsigned char link_type);

/* Max concurrent BR/EDR links (2 to support phone + headset relay). */
#define BT_CLASSIC_MAX_BR_LINK_NUM      2

#define BT_CLASSIC_A2DP_PROFILE_MASK    0x00000001
#define BT_CLASSIC_AVRCP_PROFILE_MASK   0x00000002

typedef struct {
	uint8_t  bd_addr[6];
	bool     used;
	uint8_t  id;

	uint32_t connected_profile;

	uint8_t  a2dp_codec_type;
	union {
		struct {
			uint8_t sampling_frequency;
			uint8_t channel_mode;
			uint8_t block_length;
			uint8_t subbands;
			uint8_t allocation_method;
			uint8_t min_bitpool;
			uint8_t max_bitpool;
		} sbc;
		struct {
			uint8_t  object_type;
			uint16_t sampling_frequency;
			uint8_t  channel_number;
			bool     vbr_supported;
			uint32_t bit_rate;
		} aac;
		struct {
			uint8_t  info[12];
		} vendor;
	} a2dp_codec_info;

	uint8_t  streaming_fg;
	uint8_t  avrcp_play_status;
} T_BT_CLASSIC_BR_LINK;

typedef struct {
	T_BT_CLASSIC_BR_LINK br_link[BT_CLASSIC_MAX_BR_LINK_NUM];
	uint8_t              local_addr[6];
} T_BT_CLASSIC_DB;

extern T_BT_CLASSIC_DB bt_classic_db;

T_BT_CLASSIC_BR_LINK *bt_classic_find_br_link(uint8_t *bd_addr);

T_BT_CLASSIC_BR_LINK *bt_classic_alloc_br_link(uint8_t *bd_addr);

bool bt_classic_free_br_link(T_BT_CLASSIC_BR_LINK *p_link);

void bt_classic_link_db_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __BT_CLASSIC_LINK_H__ */
