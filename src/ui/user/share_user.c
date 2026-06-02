#include "share_user.h"

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
void click_share_image_button(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    dev_mode = MODE_SHARE;
#ifdef _HONEYGUI_SIMULATOR_
    // TODO
#else
    // TODO
#endif
}

void click_receive_image_button(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    dev_mode = MODE_RECEIVE;
#ifdef _HONEYGUI_SIMULATOR_
    // TODO
#else
    // TODO
#endif
}