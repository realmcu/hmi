#include "menu_user.h"

/**
 * User-defined implementation
 * This file is generated once only, feel free to modify
 */

// Add custom implementations here

/***
 * Template function
 * Distinguish development environments
 */
// void user_defined_func_called_by_event(void *obj, gui_event_t *e)
// {
//     GUI_UNUSED(obj);
//     GUI_UNUSED(e);
// #ifdef _HONEYGUI_SIMULATOR_
//     // TODO
// #else
//     // TODO
// #endif
// }

// void user_defined_func_called_by_msg(gui_obj_t *obj, const char *topic, void *data, uint16_t len)
// {
//     GUI_UNUSED(obj);
//     GUI_UNUSED(topic);
//     GUI_UNUSED(data);
//     GUI_UNUSED(len);
// #ifdef _HONEYGUI_SIMULATOR_
//     // TODO
// #else
//     // TODO
// #endif
// }

// void list_note_design(gui_obj_t *obj, void *param)
// {
//     GUI_UNUSED(param);
//     // Cast obj to gui_list_note_t * type
//     gui_list_note_t *note = (gui_list_note_t *)obj;
//     uint16_t index = note->index;
//     GUI_UNUSED(index);
// }

/* ----------------------------------------------------*/
uint8_t menu_idx = 0;

void click_menu_reset_button(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    menu_idx = 0;
    gui_view_switch_direct(gui_view_get_current(), "menuDetailView", SWITCH_INIT_STATE,
                           SWITCH_IN_NONE_ANIMATION);

#ifdef _HONEYGUI_SIMULATOR_
    // TODO
#else
    // TODO
#endif
}

void click_menu_fs_button(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    menu_idx = 1;
    gui_view_switch_direct(gui_view_get_current(), "menuDetailView", SWITCH_INIT_STATE,
                           SWITCH_IN_NONE_ANIMATION);

#ifdef _HONEYGUI_SIMULATOR_
    // TODO
#else
    // TODO
#endif
}

void click_menu_power_off_button(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    menu_idx = 2;
    gui_view_switch_direct(gui_view_get_current(), "menuDetailView", SWITCH_INIT_STATE,
                           SWITCH_IN_NONE_ANIMATION);

#ifdef _HONEYGUI_SIMULATOR_
    // TODO
#else
    // TODO
#endif
}

void click_menu_yes_button(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    switch (menu_idx)
    {
    case 0:
        // TODO
        gui_log("click_menu_yes_button: reset\n");
        break;
    case 1:
        // TODO
        gui_log("click_menu_yes_button: format storage\n");
        break;
    case 2:
        // TODO
        gui_log("click_menu_yes_button: power off\n");
        break;

    default:
        break;
    }

#ifdef _HONEYGUI_SIMULATOR_
    // TODO
#else
    // TODO
#endif
}