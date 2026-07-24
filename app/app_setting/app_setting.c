/*
 * app_setting.c — skeleton
 *
 * TODO: forward all get/set/del to FlashDB KV. Maintain a static
 *       key-name -> app_setting_key_id_t mapping table; on successful
 *       set publish EVT_SETTING_CHANGED with the mapped id.
 */

#include "app_setting.h"

static int setting_init(void) { return 0; }

const app_module_t app_setting_module =
{
    .name  = "setting",
    .init  = setting_init,
};
