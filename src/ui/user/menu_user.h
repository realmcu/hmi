#ifndef MENU_USER_H
#define MENU_USER_H

#include "../callbacks/menu_callbacks.h"
#include "../ui/menu_ui.h"

/**
 * User-defined header file
 * This file is generated once only, feel free to modify
 */

// Add custom declarations here
extern uint8_t menu_idx; //0:Reset, 1:Format storage, 2:Power off

void click_menu_reset_button(void *obj, gui_event_t *e);
void click_menu_fs_button(void *obj, gui_event_t *e);
void click_menu_power_off_button(void *obj, gui_event_t *e);
void click_menu_yes_button(void *obj, gui_event_t *e);


#endif // MENU_USER_H
