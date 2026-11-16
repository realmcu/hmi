#include "app_phone.h"

#include <string.h>
#include "app_event.h"
#include "app_event_defs.h"
#include "hmi_bt_hfp.h"

static app_phone_state_t s_state = APP_PHONE_STATE_DISCONNECTED;

static app_phone_state_t from_hmi(hmi_phone_state_t state)
{
    switch (state)
    {
    case HMI_PHONE_IDLE: return APP_PHONE_STATE_IDLE;
    case HMI_PHONE_INCOMING: return APP_PHONE_STATE_INCOMING;
    case HMI_PHONE_DIALING: return APP_PHONE_STATE_OUTGOING;
    case HMI_PHONE_ACTIVE: return APP_PHONE_STATE_ACTIVE;
    default: return APP_PHONE_STATE_DISCONNECTED;
    }
}

static void on_phone_state(app_event_id_t id, const void *payload, size_t len, void *user)
{
    (void)id;
    (void)user;
    if (payload != NULL && len >= sizeof(uint8_t))
    {
        s_state = from_hmi((hmi_phone_state_t)*(const uint8_t *)payload);
    }
}

static int phone_init(void)
{
    s_state = from_hmi(hmi_phone_get_state());
    int rc = app_event_subscribe(EVT_PHONE_STATE_CHANGED, on_phone_state, NULL);
    return rc < 0 ? rc : 0;
}

app_phone_state_t app_phone_state(void)
{
    return s_state;
}

int app_phone_dial(const char *number)
{
    return hmi_phone_dial(number) ? 0 : -1;
}

int app_phone_answer(void)
{
    return hmi_phone_answer() ? 0 : -1;
}

int app_phone_hangup(void)
{
    return hmi_phone_hangup() ? 0 : -1;
}

int app_phone_set_muted(bool muted)
{
    return hmi_phone_set_muted(muted) ? 0 : -1;
}

int app_phone_set_watch_audio(bool on_watch)
{
    return hmi_phone_set_watch_audio(on_watch) ? 0 : -1;
}

void app_phone_peer_number(char *out, size_t size)
{
    hmi_phone_get_number(out, size);
}

bool app_phone_get_muted(void)
{
    return hmi_phone_get_muted();
}

bool app_phone_get_watch_audio(void)
{
    return hmi_phone_get_watch_audio();
}

const app_module_t app_phone_module =
{
    .name = "phone",
    .init = phone_init,
};
