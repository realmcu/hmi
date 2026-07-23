/*
 * app_phone.c — skeleton
 *
 * TODO: hook Realtek HFP callbacks, run the call state machine, feed
 *       incoming call info into the getters and publish state events.
 */

#include "app_phone.h"

static int  phone_init(void) { return 0; }
const app_module_t app_phone_module =
{
    .name  = "phone",
    .init  = phone_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

app_phone_state_t app_phone_state(void)  { return APP_PHONE_STATE_IDLE; }
int  app_phone_answer(void)              { return -1; }
int  app_phone_hangup(void)              { return -1; }
int  app_phone_reject(void)              { return -1; }
const char *app_phone_peer_number(void)  { return ""; }
const char *app_phone_peer_name(void)  { return ""; }
