#ifndef __APP_NOTIFY_H__
#define __APP_NOTIFY_H__

#include "app_module.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_notify.h
 * @brief Phone-notification cache and dispatcher.
 *
 * Receives notifications from either iOS (ANCS) or Android (the
 * companion app's private GATT service), keeps a small ring buffer of
 * recent items, and publishes @c EVT_NOTIFY_NEW / @c REMOVED / @c CLEARED
 * carrying just the notification id — the UI reads details back via the
 * getters here.
 */

/** Ring buffer depth. Older entries are overwritten. */
#define APP_NOTIFY_MAX      20

typedef struct
{
    uint32_t id;
    uint32_t timestamp;       /* unix seconds */
    uint8_t  category;        /* mirrors ANCS category */
    char     app  [24];       /* source app name, NUL-terminated */
    char     title[48];       /* NUL-terminated */
    char     body [64];       /* NUL-terminated, may be truncated */
} app_notify_item_t;

extern const app_module_t app_notify_module;

size_t                   app_notify_count(void);
const app_notify_item_t *app_notify_get(size_t index);     /* 0 = newest */
int                      app_notify_dismiss(uint32_t id);
int                      app_notify_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_NOTIFY_H__ */
