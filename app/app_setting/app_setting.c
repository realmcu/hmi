/*
 * app_setting.c — skeleton
 *
 * TODO: forward all get/set/del to FlashDB KV. Maintain a static
 *       key-name -> app_setting_key_id_t mapping table; on successful
 *       set publish EVT_SETTING_CHANGED with the mapped id.
 */

#include "app_setting.h"

#include <stddef.h>

static int  setting_init(void) { return 0; }
const app_module_t app_setting_module =
{
    .name  = "setting",
    .init  = setting_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

int  app_setting_get_u32(const char *key, uint32_t *out, uint32_t default_val)
{
    (void)key;
    if (out != NULL)
    {
        *out = default_val;
    }
    return -1;
}

int  app_setting_set_u32(const char *key, uint32_t val)
{
    (void)key; (void)val;
    return -1;
}

int  app_setting_get_str(const char *key, char *out, size_t out_size, const char *default_val)
{
    (void)key; (void)default_val;
    if (out != NULL && out_size > 0)
    {
        out[0] = '\0';
    }
    return -1;
}

int  app_setting_set_str(const char *key, const char *val)
{
    (void)key; (void)val;
    return -1;
}

int  app_setting_get_blob(const char *key, void *out, size_t *inout_size)
{
    (void)key; (void)out;
    if (inout_size != NULL)
    {
        *inout_size = 0;
    }
    return -1;
}

int  app_setting_set_blob(const char *key, const void *val, size_t size)
{
    (void)key; (void)val; (void)size;
    return -1;
}

int  app_setting_del(const char *key)
{
    (void)key;
    return -1;
}
