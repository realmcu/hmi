#ifndef __APP_PHONE_H__
#define __APP_PHONE_H__

#include <stdbool.h>
#include <stddef.h>
#include "app_module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_PHONE_STATE_IDLE,
    APP_PHONE_STATE_INCOMING,
    APP_PHONE_STATE_OUTGOING,
    APP_PHONE_STATE_ACTIVE,
    APP_PHONE_STATE_DISCONNECTED,
} app_phone_state_t;

extern const app_module_t app_phone_module;

app_phone_state_t app_phone_state(void);
int app_phone_dial(const char *number);
int app_phone_answer(void);
int app_phone_hangup(void);
int app_phone_set_muted(bool muted);
int app_phone_set_watch_audio(bool on_watch);
void app_phone_peer_number(char *out, size_t size);
bool app_phone_get_muted(void);
bool app_phone_get_watch_audio(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PHONE_H__ */
