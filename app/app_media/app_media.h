#ifndef __APP_MEDIA_H__
#define __APP_MEDIA_H__

#include <stdbool.h>
#include <stdint.h>
#include "app_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_media.h
 * @brief A2DP sink state and AVRCP metadata / transport controls.
 */
extern const app_module_t app_media_module;

typedef enum
{
    APP_MEDIA_STATE_IDLE,
    APP_MEDIA_STATE_PLAYING,
    APP_MEDIA_STATE_PAUSED,
} app_media_state_t;

void app_media_audio_ready(void);
app_media_state_t app_media_state(void);
bool app_media_connected(void);
int app_media_play(void);
int app_media_pause(void);
int app_media_next(void);
int app_media_prev(void);
int app_media_volume_set(uint8_t percent);
uint8_t app_media_volume_get(void);
const char *app_media_track_title(void);
const char *app_media_track_artist(void);

/* Local microphone capture: PCM 16 kHz, mono, signed 16-bit. The captured
 * buffer is allocated on demand from HoneyGUI's large (PSRAM) heap. */
bool app_media_record_start(void);
bool app_media_record_start_from_ui(void);
void app_media_record_stop(void);
bool app_media_record_active(void);
bool app_media_record_info(uint32_t *id, uint32_t *bytes);
uint16_t app_media_record_read(uint32_t id, uint32_t offset, uint8_t *out, uint16_t len);
uint32_t app_media_record_live_bytes(void);
uint16_t app_media_record_live_read(uint32_t offset, uint8_t *out, uint16_t len);
bool app_media_speaker_free(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MEDIA_H__ */
