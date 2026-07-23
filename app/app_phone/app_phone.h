#ifndef __APP_PHONE_H__
#define __APP_PHONE_H__

#include "app_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_phone.h
 * @brief HFP call state machine for the watch.
 *
 * Publishes @c EVT_PHONE_STATE_CHANGED on state transitions and
 * @c EVT_PHONE_RING once per fresh incoming call (drives vibration /
 * ringtone). Peer info strings are held internally; pointers returned by
 * @c app_phone_peer_number / _peer_name are valid until the next state
 * change.
 */

typedef enum
{
    APP_PHONE_STATE_IDLE = 0,
    APP_PHONE_STATE_INCOMING,   /* ringing */
    APP_PHONE_STATE_OUTGOING,   /* dialing */
    APP_PHONE_STATE_ACTIVE,     /* on a call */
} app_phone_state_t;

extern const app_module_t app_phone_module;

app_phone_state_t app_phone_state(void);

int  app_phone_answer(void);
int  app_phone_hangup(void);
int  app_phone_reject(void);

const char *app_phone_peer_number(void);
const char *app_phone_peer_name(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PHONE_H__ */
