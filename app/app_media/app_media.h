#ifndef __APP_MEDIA_H__
#define __APP_MEDIA_H__

#include "app_module.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_media.h
 * @brief A2DP sink state and AVRCP metadata / transport controls.
 *
 * Also owns "playback focus" — e.g. must duck / pause when @c app_phone
 * enters INCOMING or ACTIVE. Metadata strings are held internally; the
 * pointers returned by @c app_media_track_title / _artist are valid
 * until the next @c EVT_MEDIA_METADATA .
 */

typedef enum
{
    APP_MEDIA_STATE_IDLE = 0,
    APP_MEDIA_STATE_PLAYING,
    APP_MEDIA_STATE_PAUSED,
} app_media_state_t;

extern const app_module_t app_media_module;

app_media_state_t app_media_state(void);

int     app_media_play(void);
int     app_media_pause(void);
int     app_media_next(void);
int     app_media_prev(void);

int     app_media_volume_set(uint8_t percent);
uint8_t app_media_volume_get(void);

const char *app_media_track_title(void);
const char *app_media_track_artist(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MEDIA_H__ */
