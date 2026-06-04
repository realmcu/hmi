#ifndef EASY_DEMOMAIN_CALLBACKS_H
#define EASY_DEMOMAIN_CALLBACKS_H

#include "gui_api.h"
#include "gui_text.h"
#include "gui_obj_focus.h"

// Timer animation counters (defined in callbacks.c)
extern uint16_t easy_demoMainView_timer_cnt;
extern uint16_t mainface_view_1_timer_cnt;
extern uint16_t mainface_view_2_timer_cnt;
extern uint16_t mainface_view_3_timer_cnt;
extern uint16_t mainface_view_4_timer_cnt;
extern uint16_t mainface_view_5_timer_cnt;
extern uint16_t mainface_view_6_timer_cnt;
extern uint16_t mainface_view_7_timer_cnt;
extern uint16_t top_view_timer_cnt;
extern uint16_t bg_circle_timer_cnt;
extern uint16_t icon_bat_timer_cnt;
extern uint16_t lbl_1_timer_cnt;

// Event callback function declarations
void easy_demoMainView_key_0_cb(void *obj, gui_event_t *e);
void easy_demoMainView_key_1_cb(void *obj, gui_event_t *e);
void icon_as_clicked_cb(void *obj, gui_event_t *e);
void icon_connect_clicked_cb(void *obj, gui_event_t *e);
void icon_del_clicked_cb(void *obj, gui_event_t *e);
void icon_fl_clicked_cb(void *obj, gui_event_t *e);
void icon_sl_clicked_cb(void *obj, gui_event_t *e);
void mainface_view_1_key_0_cb(void *obj, gui_event_t *e);
void mainface_view_1_key_1_cb(void *obj, gui_event_t *e);
void mainface_view_2_key_0_cb(void *obj, gui_event_t *e);
void mainface_view_2_key_1_cb(void *obj, gui_event_t *e);
void mainface_view_3_key_0_cb(void *obj, gui_event_t *e);
void mainface_view_3_key_1_cb(void *obj, gui_event_t *e);
void mainface_view_4_key_0_cb(void *obj, gui_event_t *e);
void mainface_view_4_key_1_cb(void *obj, gui_event_t *e);
void mainface_view_5_key_0_cb(void *obj, gui_event_t *e);
void mainface_view_5_key_1_cb(void *obj, gui_event_t *e);
void mainface_view_6_key_0_cb(void *obj, gui_event_t *e);
void mainface_view_6_key_1_cb(void *obj, gui_event_t *e);
void mainface_view_7_key_0_cb(void *obj, gui_event_t *e);
void mainface_view_7_key_1_cb(void *obj, gui_event_t *e);
void top_view_key_0_cb(void *obj, gui_event_t *e);
void top_view_key_1_cb(void *obj, gui_event_t *e);
void view_fl_key_0_cb(void *obj, gui_event_t *e);

// User-configured timer callback function declarations
void easy_demoMainView_timer_0_cb(void *obj);
void mainface_view_1_timer_0_cb(void *obj);
void mainface_view_2_timer_0_cb(void *obj);
void mainface_view_3_timer_0_cb(void *obj);
void mainface_view_4_timer_0_cb(void *obj);
void mainface_view_5_timer_0_cb(void *obj);
void mainface_view_6_timer_0_cb(void *obj);
void mainface_view_7_timer_0_cb(void *obj);
void top_view_timer_0_cb(void *obj);
void bg_circle_timer_0_cb(void *obj);
void icon_bat_timer_0_cb(void *obj);
void lbl_1_timer_0_cb(void *obj);

// Custom function declarations (auto-extracted from callbacks.c protected area)
void bg_circle_timer_0_cb(void *obj);
void top_view_timer_0_cb(void *obj);
void easy_demoMainView_update_idx_cb(void *obj);
void mainface_view_1_update_idx_cb(void *obj);
void mainface_view_2_update_idx_cb(void *obj);
void mainface_view_3_update_idx_cb(void *obj);
void mainface_view_4_update_idx_cb(void *obj);
void mainface_view_5_update_idx_cb(void *obj);
void mainface_view_6_update_idx_cb(void *obj);
void mainface_view_7_update_idx_cb(void *obj);
void easy_demoMainView_timer_0_cb(void *obj);
void mainface_view_1_timer_0_cb(void *obj);
void mainface_view_2_timer_0_cb(void *obj);
void mainface_view_3_timer_0_cb(void *obj);
void mainface_view_4_timer_0_cb(void *obj);
void mainface_view_5_timer_0_cb(void *obj);
void mainface_view_6_timer_0_cb(void *obj);
void mainface_view_7_timer_0_cb(void *obj);
void lbl_1_timer_0_cb(void *obj);
void icon_bat_timer_0_cb(void *obj);

#endif // EASY_DEMOMAIN_CALLBACKS_H
