#ifndef __APP_RECORD_H__
#define __APP_RECORD_H__

#include <stdbool.h>
#include <stdint.h>

#include "app_module.h"

#define APP_RECORD_PCM_SAMPLE_RATE    16000u
#define APP_RECORD_PCM_FRAME_SAMPLES  240u
#define APP_RECORD_PCM_BYTES_PER_MS   32u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_RECORD_STOP_COMPLETED = 0,
    APP_RECORD_STOP_CANCELLED_BY_PHONE = 1,
    APP_RECORD_STOP_AUDIO_ERROR = 2,
    APP_RECORD_STOP_TRANSPORT_ERROR = 3,
    APP_RECORD_STOP_REQUEST_REJECTED = 4,
} app_record_stop_reason_t;

/* Registers event subscriptions during app-module bring-up. */
int app_record_init(void);

/* Marks the audio subsystem ready; called after audio_mgr_init succeeds. */
void app_record_audio_ready(void);

/* Forward all audio manager events here. Non-recording events are ignored. */
void app_record_handle_audio_event(uint32_t event, void *param, uint16_t len);

bool app_record_start(void);
void app_record_stop(void);
bool app_record_is_active(void);

extern const app_module_t app_record_module;

#ifdef __cplusplus
}
#endif

#endif /* __APP_RECORD_H__ */
