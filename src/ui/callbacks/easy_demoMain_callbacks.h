#ifndef EASY_DEMOMAIN_CALLBACKS_H
#define EASY_DEMOMAIN_CALLBACKS_H

#include "gui_api.h"
#include "gui_text.h"
#include "gui_obj_focus.h"

// Timer animation counters (defined in callbacks.c)
extern uint16_t easy_demoMainView_timer_cnt;
extern uint16_t img_2_timer_cnt;

// Event callback function declarations

// User-configured timer callback function declarations
void easy_demoMainView_timer_0_cb(void *obj);
void easy_demoMainView_timer_init_cb(void *obj);
void img_2_timer_0_cb(void *obj);

// Custom function declarations (auto-extracted from callbacks.c protected area)
void switch_watchface(void *obj);
void easy_demoMainView_timer_init_cb(void *obj);
void easy_demoMainView_timer_0_cb(void *obj);

#endif // EASY_DEMOMAIN_CALLBACKS_H
