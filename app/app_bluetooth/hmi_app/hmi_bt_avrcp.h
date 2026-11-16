/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#ifndef _HMI_BT_AVRCP_H_
#define _HMI_BT_AVRCP_H_

#include <stdint.h>
#include <stdbool.h>

#define HMI_MEDIA_IO_MSG_TYPE        0x7001u
#define HMI_MEDIA_VOLUME_IO_MSG_TYPE 0x7002u

typedef enum
{
    HMI_MEDIA_TOGGLE,
    HMI_MEDIA_PREVIOUS,
    HMI_MEDIA_NEXT,
    HMI_MEDIA_VOLUME_DOWN,
    HMI_MEDIA_VOLUME_UP,
    HMI_MEDIA_REFRESH,
} hmi_media_command_t;

#define HMI_MEDIA_NAME_MAX 96

typedef struct
{
    bool connected;
    bool playing;
    bool has_position;
    uint32_t position_ms;
    uint32_t length_ms;
    char title[HMI_MEDIA_NAME_MAX];
    char artist[HMI_MEDIA_NAME_MAX];
} hmi_media_snapshot_t;

void hmi_bt_avrcp_init(void);

/* Media requests are marshalled to bt_task before touching AVRCP. */
bool hmi_media_send(hmi_media_command_t command);
bool hmi_media_set_volume(uint8_t percent);
void hmi_media_handle_volume(void *value);    /* bt_task only */
void hmi_media_handle_command(void *command); /* bt_task only */
void hmi_media_snapshot(hmi_media_snapshot_t *snapshot);

/* Update local play status so TG can respond to registration requests correctly */
void hmi_bt_avrcp_play_status_update(uint8_t *bd_addr, bool playing);

/* Update local absolute volume (0-127) so TG can respond to registration requests */
void hmi_bt_avrcp_volume_update(uint8_t *bd_addr, uint8_t vol_0_127);

/* Called when remote TG notifies a play status change */
void hmi_bt_avrcp_set_play_status_cb(void (*cb)(uint8_t *bd_addr, uint8_t play_status));

/* Called when remote TG sets absolute volume (0-127) */
void hmi_bt_avrcp_set_volume_cb(void (*cb)(uint8_t *bd_addr, uint8_t vol_0_127));

#endif /* _HMI_BT_AVRCP_H_ */
