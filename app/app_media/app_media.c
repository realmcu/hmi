/*
 * app_media.c — skeleton
 *
 * TODO: hook Realtek A2DP sink callbacks, drive AVRCP transport /
 *       metadata queries, own duck/resume policy for phone integration.
 */

#include "app_media.h"

static int  media_init(void) { return 0; }
const app_module_t app_media_module =
{
    .name  = "media",
    .init  = media_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

app_media_state_t app_media_state(void)          { return APP_MEDIA_STATE_IDLE; }
int      app_media_play(void)                   { return -1; }
int      app_media_pause(void)                   { return -1; }
int      app_media_next(void)                   { return -1; }
int      app_media_prev(void)                   { return -1; }
int      app_media_volume_set(uint8_t p)         { (void)p; return -1; }
uint8_t  app_media_volume_get(void)              { return 0; }
const char *app_media_track_title(void)         { return ""; }
const char *app_media_track_artist(void)         { return ""; }
