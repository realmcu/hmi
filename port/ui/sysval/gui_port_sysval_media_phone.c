/*
 * A2DP/AVRCP and HFP sysval adapters.
 */

#include "app_media.h"
#include "app_phone.h"

#include "gui_components_init.h"
#include "gui_port_sysval_common.h"
#include "gui_sysval_keys.h"

#include <string.h>

static const char *media_state_value(void)
{
    if (!app_media_connected())
    {
        return GUI_SYSVAL_MEDIA_STATE_UNAVAILABLE;
    }
    switch (app_media_state())
    {
    case APP_MEDIA_STATE_PLAYING: return GUI_SYSVAL_MEDIA_STATE_PLAYING;
    case APP_MEDIA_STATE_PAUSED:  return GUI_SYSVAL_MEDIA_STATE_PAUSED;
    default:                      return GUI_SYSVAL_MEDIA_STATE_STOPPED;
    }
}

static const char *phone_state_value(void)
{
    switch (app_phone_state())
    {
    case APP_PHONE_STATE_INCOMING: return GUI_SYSVAL_PHONE_STATE_INCOMING;
    case APP_PHONE_STATE_OUTGOING: return GUI_SYSVAL_PHONE_STATE_OUTGOING;
    case APP_PHONE_STATE_ACTIVE:   return GUI_SYSVAL_PHONE_STATE_ACTIVE;
    case APP_PHONE_STATE_IDLE:     return GUI_SYSVAL_PHONE_STATE_IDLE;
    default:                       return GUI_SYSVAL_PHONE_STATE_UNAVAILABLE;
    }
}

static const char *phone_route_value(void)
{
    if (app_phone_state() == APP_PHONE_STATE_DISCONNECTED)
    {
        return GUI_SYSVAL_PHONE_ROUTE_UNAVAILABLE;
    }
    return app_phone_get_watch_audio() ? GUI_SYSVAL_PHONE_ROUTE_DEVICE :
                                         GUI_SYSVAL_PHONE_ROUTE_PHONE;
}

static void get_media_state(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_str(request, media_state_value());
}

static void get_media_volume(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_int(request, app_media_volume_get());
}

static void get_media_title(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_str(request, app_media_track_title());
}

static void get_media_artist(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_str(request, app_media_track_artist());
}

static void get_phone_state(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_str(request, phone_state_value());
}

static void get_phone_number(gui_sysval_request_t *request)
{
    char number[GUI_SYSVAL_STR_MAX];
    app_phone_peer_number(number, sizeof(number));
    gui_port_sysval_respond_str(request, number);
}

static void get_phone_route(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_str(request, phone_route_value());
}

static void get_audio_muted(gui_sysval_request_t *request)
{
    gui_port_sysval_respond_bool(request, app_phone_get_muted());
}

static void set_media_volume(const gui_sysval_value_t *value,
                             gui_sysval_request_t *request)
{
    int32_t volume = value->data.i;
    if (volume < 0 || volume > 100)
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_INVALID_VALUE, NULL);
        return;
    }
    if (app_media_volume_set((uint8_t)volume) != 0)
    {
        gui_port_sysval_respond_error(request);
        return;
    }
    gui_port_sysval_respond_int(request, volume);
}

static void set_audio_muted(const gui_sysval_value_t *value,
                            gui_sysval_request_t *request)
{
    if (app_phone_set_muted(value->data.b) != 0)
    {
        gui_port_sysval_respond_error(request);
        return;
    }
    gui_port_sysval_respond_bool(request, value->data.b);
}

static void set_phone_route(const gui_sysval_value_t *value,
                            gui_sysval_request_t *request)
{
    bool on_watch;
    if (strcmp(value->data.s, GUI_SYSVAL_PHONE_ROUTE_DEVICE) == 0)
    {
        on_watch = true;
    }
    else if (strcmp(value->data.s, GUI_SYSVAL_PHONE_ROUTE_PHONE) == 0)
    {
        on_watch = false;
    }
    else
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_INVALID_VALUE, NULL);
        return;
    }

    if (app_phone_set_watch_audio(on_watch) != 0)
    {
        gui_port_sysval_respond_error(request);
        return;
    }
    gui_port_sysval_respond_str(request, value->data.s);
}

static void set_phone_request(const gui_sysval_value_t *value,
                              gui_sysval_request_t *request)
{
    const char *command = value->data.s;
    int rc;

    if (strcmp(command, "answer") == 0)
    {
        rc = app_phone_answer();
    }
    else if (strcmp(command, "hangup") == 0)
    {
        rc = app_phone_hangup();
    }
    else if (strncmp(command, "dial:", 5) == 0 && command[5] != '\0')
    {
        rc = app_phone_dial(&command[5]);
    }
    else
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_INVALID_VALUE, NULL);
        return;
    }

    if (rc != 0)
    {
        gui_port_sysval_respond_error(request);
        return;
    }
    gui_port_sysval_respond_str(request, command);
}

static void set_media_request(const gui_sysval_value_t *value,
                              gui_sysval_request_t *request)
{
    const char *command = value->data.s;
    int rc;

    if (strcmp(command, "play") == 0)
    {
        rc = app_media_play();
    }
    else if (strcmp(command, "pause") == 0)
    {
        rc = app_media_pause();
    }
    else if (strcmp(command, "toggle") == 0)
    {
        rc = app_media_state() == APP_MEDIA_STATE_PLAYING ?
             app_media_pause() : app_media_play();
    }
    else if (strcmp(command, "next") == 0)
    {
        rc = app_media_next();
    }
    else if (strcmp(command, "previous") == 0)
    {
        rc = app_media_prev();
    }
    else if (strcmp(command, "record_start") == 0)
    {
        rc = app_media_record_active() ||
             app_media_record_start_from_ui() ? 0 : -1;
    }
    else if (strcmp(command, "record_stop") == 0)
    {
        if (app_media_record_active())
        {
            app_media_record_stop();
        }
        rc = 0;
    }
    else if (strcmp(command, "record_toggle") == 0)
    {
        if (app_media_record_active())
        {
            app_media_record_stop();
            rc = 0;
        }
        else
        {
            rc = app_media_record_start_from_ui() ? 0 : -1;
        }
    }
    else
    {
        gui_sysval_respond(request, GUI_SYSVAL_STATUS_INVALID_VALUE, NULL);
        return;
    }

    if (rc != 0)
    {
        gui_port_sysval_respond_error(request);
        return;
    }
    gui_port_sysval_respond_str(request, command);
}

static gui_sysval_adapter_t media_state_adapter = {
    .key = GUI_SYSVAL_KEY_MEDIA_MUSIC_STATE,
    .type = GUI_SYSVAL_TYPE_STR,
    .get_request = get_media_state,
};

static gui_sysval_adapter_t media_volume_adapter = {
    .key = GUI_SYSVAL_KEY_MEDIA_MUSIC_VOLUME,
    .type = GUI_SYSVAL_TYPE_INT,
    .get_request = get_media_volume,
    .set_request = set_media_volume,
};

static gui_sysval_adapter_t media_title_adapter = {
    .key = GUI_SYSVAL_KEY_MEDIA_MUSIC_TITLE,
    .type = GUI_SYSVAL_TYPE_STR,
    .get_request = get_media_title,
};

static gui_sysval_adapter_t media_artist_adapter = {
    .key = GUI_SYSVAL_KEY_MEDIA_MUSIC_ARTIST,
    .type = GUI_SYSVAL_TYPE_STR,
    .get_request = get_media_artist,
};

static gui_sysval_adapter_t audio_muted_adapter = {
    .key = GUI_SYSVAL_KEY_AUDIO_MUTED,
    .type = GUI_SYSVAL_TYPE_BOOL,
    .get_request = get_audio_muted,
    .set_request = set_audio_muted,
};

static gui_sysval_adapter_t phone_state_adapter = {
    .key = GUI_SYSVAL_KEY_PHONE_STATE,
    .type = GUI_SYSVAL_TYPE_STR,
    .get_request = get_phone_state,
};

static gui_sysval_adapter_t phone_number_adapter = {
    .key = GUI_SYSVAL_KEY_PHONE_NUMBER,
    .type = GUI_SYSVAL_TYPE_STR,
    .get_request = get_phone_number,
};

static gui_sysval_adapter_t phone_route_adapter = {
    .key = GUI_SYSVAL_KEY_PHONE_AUDIO_ROUTE,
    .type = GUI_SYSVAL_TYPE_STR,
    .get_request = get_phone_route,
    .set_request = set_phone_route,
};

static gui_sysval_adapter_t phone_request_adapter = {
    .key = GUI_SYSVAL_KEY_PHONE_REQUEST,
    .type = GUI_SYSVAL_TYPE_STR,
    .set_request = set_phone_request,
};

static gui_sysval_adapter_t media_request_adapter = {
    .key = GUI_SYSVAL_KEY_MEDIA_REQUEST,
    .type = GUI_SYSVAL_TYPE_STR,
    .set_request = set_media_request,
};

static int gui_port_sysval_media_phone_init(void)
{
    gui_sysval_adapter_t *const adapters[] = {
        &media_state_adapter,
        &media_volume_adapter,
        &media_title_adapter,
        &media_artist_adapter,
        &audio_muted_adapter,
        &phone_state_adapter,
        &phone_number_adapter,
        &phone_route_adapter,
        &phone_request_adapter,
        &media_request_adapter,
    };
    return gui_port_sysval_register_adapters(
        adapters, sizeof(adapters) / sizeof(adapters[0]));
}

GUI_INIT_DEVICE_EXPORT(gui_port_sysval_media_phone_init);
