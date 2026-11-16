/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
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

/* The sink is ready to render the negotiated SBC stream. Returning false
 * rejects the stream start without taking the phone's audio path. */
void hmi_bt_a2dp_set_sink_start_cb(
    bool (*cb)(uint8_t *bd_addr, T_HMI_A2DP_CODEC_SBC *codec));

/* Full A2DP media packet. audio_track_write() needs the Bluetooth clock and
 * sequence number, so the legacy snk-data callback alone is not sufficient. */
void hmi_bt_a2dp_set_packet_cb(
    void (*cb)(uint8_t *bd_addr, uint32_t bt_clock, uint16_t seq_num,
               uint8_t frame_num, uint8_t *payload, uint16_t len));

void hmi_bt_a2dp_set_stream_start_cb(void (*cb)(uint8_t *bd_addr, uint8_t role));

void hmi_bt_a2dp_set_stream_stop_cb(void (*cb)(uint8_t *bd_addr));

#endif /* _HMI_BT_A2DP_H_ */
