#ifndef SHARE_CALLBACKS_H
#define SHARE_CALLBACKS_H

#include "gui_api.h"
#include "gui_text.h"
#include "gui_obj_focus.h"

// Timer animation counters (defined in callbacks.c)
extern uint16_t circle_anime_timer_cnt;
extern uint16_t lbl_share_timer_cnt;
extern uint16_t win_share_3_timer_cnt;
extern uint16_t img_8_timer_cnt;

// Event callback function declarations
void img_4_clicked_cb(void *obj, gui_event_t *e);
void img_5_clicked_cb(void *obj, gui_event_t *e);

// User-configured timer callback function declarations
void circle_anime_timer_0_cb(void *obj);
void lbl_share_timer_0_cb(void *obj);
void win_share_3_timer_0_cb(void *obj);
void img_8_timer_0_cb(void *obj);

// Custom function declarations (auto-extracted from callbacks.c protected area)
void lbl_share_timer_0_cb(void *obj);
void win_share_3_timer_0_cb(void *obj);

#endif // SHARE_CALLBACKS_H
