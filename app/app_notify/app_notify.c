/*
 * app_notify.c — skeleton
 *
 * TODO: subscribe to ANCS + companion-app private notifications, keep a
 *       ring buffer of recent items, publish EVT_NOTIFY_* .
 */

#include "app_notify.h"

static int notify_init(void) { return 0; }

const app_module_t app_notify_module =
{
    .name  = "notify",
    .init  = notify_init,
};
