/*
 * app_display.c
 *
 * Display bring-up. GUI server startup lives here so main only orchestrates
 * the lower-level system and app-core bring-up.
 */

#include "app_display.h"

#include "gui_server.h"
#include "gui_message.h"

static int app_display_init(void)
{
    gui_server_init();
    gui_set_keep_active_time(0xFFFFFFFFu);
    return 0;
}

const app_module_t app_display_module =
{
    .name = "display",
    .init = app_display_init,
};
