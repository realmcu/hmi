#ifndef MENU_CALLBACKS_H
#define MENU_CALLBACKS_H

#include "gui_api.h"
#include "gui_text.h"
#include "gui_obj_focus.h"

// Timer animation counters (defined in callbacks.c)
extern uint16_t lbl_option_timer_cnt;

// Event callback function declarations
void click_menu_fs_button(void *obj, gui_event_t *e);
void click_menu_power_off_button(void *obj, gui_event_t *e);
void click_menu_reset_button(void *obj, gui_event_t *e);
void img_16_clicked_cb(void *obj, gui_event_t *e);
void img_17_clicked_cb(void *obj, gui_event_t *e);
void menuDetailView_key_0_cb(void *obj, gui_event_t *e);
void menuMainView_key_0_cb(void *obj, gui_event_t *e);

// User-configured timer callback function declarations
void lbl_option_timer_0_cb(void *obj);

// Custom function declarations (auto-extracted from callbacks.c protected area)
void lbl_option_timer_0_cb(void *obj);

#endif // MENU_CALLBACKS_H
