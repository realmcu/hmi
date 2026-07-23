#ifndef __APP_SETTING_H__
#define __APP_SETTING_H__

#include "app_module.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_setting.h
 * @brief Thin semantic layer over the FlashDB KV store.
 *
 * The final implementation forwards every call to FlashDB KV. On
 * successful @c set_* the module publishes @c EVT_SETTING_CHANGED with
 * the key's enum id (see @c app_setting_key_id_t in @c app_event_defs.h ).
 *
 * Naming convention for keys (not enforced, documented here):
 *   dot-separated, category first, then leaf — e.g.
 *   "user.age", "display.brightness", "vibrate.enable".
 */
extern const app_module_t app_setting_module;

int  app_setting_get_u32(const char *key, uint32_t *out, uint32_t default_val);
int  app_setting_set_u32(const char *key, uint32_t val);

int  app_setting_get_str(const char *key, char *out, size_t out_size, const char *default_val);
int  app_setting_set_str(const char *key, const char *val);

int  app_setting_get_blob(const char *key, void *out, size_t *inout_size);
int  app_setting_set_blob(const char *key, const void *val, size_t size);

int  app_setting_del(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SETTING_H__ */
