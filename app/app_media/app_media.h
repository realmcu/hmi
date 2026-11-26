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

/* True while an A2DP playback audio track owns the output path. */
bool app_media_track_active(void);
bool app_media_speaker_free(void);

/** Play one voice prompt from the VP partition for on-device verification. */
bool app_media_voice_prompt_play(uint8_t index, uint8_t language, bool relay);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MEDIA_H__ */
