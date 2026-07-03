/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _HMI_BT_A2DP_H_
#define _HMI_BT_A2DP_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint8_t sampling_frequency;
    uint8_t channel_mode;
    uint8_t block_length;
    uint8_t subbands;
    uint8_t allocation_method;
} T_HMI_A2DP_CODEC_SBC;

void hmi_bt_a2dp_init(void);

void hmi_bt_a2dp_set_config_cb(
    void (*cb)(uint8_t *bd_addr, uint8_t role, T_HMI_A2DP_CODEC_SBC *codec));

void hmi_bt_a2dp_set_snk_data_cb(
    void (*cb)(uint8_t *bd_addr, uint8_t frame_num, uint8_t *payload, uint16_t len));

void hmi_bt_a2dp_set_stream_start_cb(void (*cb)(uint8_t *bd_addr, uint8_t role));

void hmi_bt_a2dp_set_stream_stop_cb(void (*cb)(uint8_t *bd_addr));

bool hmi_bt_a2dp_src_send(uint8_t *bd_addr, uint16_t seq_num, uint32_t timestamp,
                          uint8_t frames_per_pkt, uint8_t *payload, uint16_t len);

#endif /* _HMI_BT_A2DP_H_ */
