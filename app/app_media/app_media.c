/*
 * app_media.c — skeleton
 *
 * TODO: hook Realtek A2DP sink callbacks, drive AVRCP transport /
 *       metadata queries, own duck/resume policy for phone integration.
 */

#include "app_media.h"

static int media_init(void) { return 0; }

const app_module_t app_media_module =
{
    .name  = "media",
    .init  = media_init,
};
