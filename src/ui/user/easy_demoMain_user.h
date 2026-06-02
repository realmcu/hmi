#ifndef EASY_DEMOMAIN_USER_H
#define EASY_DEMOMAIN_USER_H

#include "../callbacks/easy_demoMain_callbacks.h"
#include "../ui/easy_demoMain_ui.h"

/**
 * User-defined header file
 * This file is generated once only, feel free to modify
 */

// Add custom declarations here
#define  SCREEN_H  360
#define  MAINFACE_NUM  10


typedef enum
{
    SRC_IMG = 0,
    SRC_VIDEO,
} MAINFACE_SRC_TYPE;

typedef enum
{
    MODE_DEFAULT = 0,
    MODE_DELETE,
    MODE_SHARE,
    MODE_RECEIVE,
} MODE_TYPE;

extern uint8_t mainface_idx;
extern uint8_t mainface_num;
extern MAINFACE_SRC_TYPE mainface_src_type;
extern void *mainface_list[MAINFACE_NUM];

extern bool is_auto_sleep_mode;
extern bool is_bt_connect;
extern MODE_TYPE dev_mode;

extern uint8_t soc_val;
extern uint8_t screen_light_idx;

void switch_mainface(gui_obj_t *parent, uint8_t idx);
void mainface_list_delete(void);

void click_auto_sleep_icon(void *obj, gui_event_t *e);
void click_screen_light_icon(void *obj, gui_event_t *e);
void click_delete_icon(void *obj, gui_event_t *e);
void click_delete_icon_detail(void *obj, gui_event_t *e);
void click_back_icon(void *obj, gui_event_t *e);

#endif // EASY_DEMOMAIN_USER_H
