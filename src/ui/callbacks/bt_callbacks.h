#ifndef BT_CALLBACKS_H
#define BT_CALLBACKS_H

#include "gui_api.h"
#include "gui_text.h"
#include "gui_obj_focus.h"

// Timer animation counters (defined in callbacks.c)
extern uint16_t circle_scale_timer_cnt;
extern uint16_t bt_icon_timer_cnt;

// Event callback function declarations
void bt_View_key_0_cb(void *obj, gui_event_t *e);
void qrcode_view_key_0_cb(void *obj, gui_event_t *e);

// User-configured timer callback function declarations
void circle_scale_timer_0_cb(void *obj);
void bt_icon_timer_0_cb(void *obj);

// Custom function declarations (auto-extracted from callbacks.c protected area)
void bt_icon_timer_0_cb(void *obj);

#endif // BT_CALLBACKS_H
