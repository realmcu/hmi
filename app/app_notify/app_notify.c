/*
 * app_notify.c — skeleton
 *
 * TODO: subscribe to ANCS + companion-app private notifications, keep a
 *       ring buffer of APP_NOTIFY_MAX items, publish EVT_NOTIFY_* .
 */

#include "app_notify.h"

#include <stddef.h>

static int  notify_init(void) { return 0; }
const app_module_t app_notify_module =
{
    .name  = "notify",
    .init  = notify_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

size_t                   app_notify_count(void)               { return 0; }
const app_notify_item_t *app_notify_get(size_t index)       { (void)index; return NULL; }
int                      app_notify_dismiss(uint32_t id)    { (void)id; return -1; }
int                      app_notify_clear_all(void)           { return -1; }
