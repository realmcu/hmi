#include "menu_callbacks.h"
#include "../ui/menu_ui.h"
#include "../user/menu_user.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Timer animation counters
uint16_t lbl_option_timer_cnt = 0;

// Event callback function implementations

void menuMainView_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "easy_demoMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void menuDetailView_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "easy_demoMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void img_16_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    click_menu_yes_button(obj, e);
}

void img_17_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                           SWITCH_IN_NONE_ANIMATION);
}

/* @protected start custom_functions */
// Custom functions
void lbl_option_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    void *content = NULL;
    switch (menu_idx)
    {
    case 0:
        content = "Reset";
        break;
    case 1:
        content = "Format storage";
        break;
    case 2:
        content = "Power off";
        break;

    default:
        break;
    }
    gui_text_content_set((gui_text_t *)lbl_option, content, strlen(content));
    gui_obj_stop_timer(obj);
}
/* @protected end custom_functions */
