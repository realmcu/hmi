#include "easy_demoMain_callbacks.h"
#include "../ui/easy_demoMain_ui.h"
#include "../user/easy_demoMain_user.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Timer animation counters
uint16_t easy_demoMainView_timer_cnt = 0;
uint16_t mainface_view_1_timer_cnt = 0;
uint16_t mainface_view_2_timer_cnt = 0;
uint16_t mainface_view_3_timer_cnt = 0;
uint16_t mainface_view_4_timer_cnt = 0;
uint16_t mainface_view_5_timer_cnt = 0;
uint16_t mainface_view_6_timer_cnt = 0;
uint16_t mainface_view_7_timer_cnt = 0;
uint16_t top_view_timer_cnt = 0;
uint16_t bg_circle_timer_cnt = 0;
uint16_t icon_bat_timer_cnt = 0;
uint16_t lbl_1_timer_cnt = 0;

// Event callback function implementations

void easy_demoMainView_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void easy_demoMainView_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_1_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_1_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_2_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_2_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_3_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_3_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_4_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_4_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_5_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_5_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_6_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_6_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_7_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void mainface_view_7_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_INIT_STATE,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void top_view_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "bt_View", SWITCH_OUT_NONE_ANIMATION,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void top_view_key_1_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "menuMainView", SWITCH_OUT_NONE_ANIMATION,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

void icon_del_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    click_delete_icon(obj, e);
}

void icon_fl_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    gui_view_switch_direct(gui_view_get_current(), "view_fl", SWITCH_INIT_STATE,
                           SWITCH_IN_NONE_ANIMATION);
}

void icon_connect_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    click_share_icon(obj, e);
}

void icon_sl_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    click_screen_light_icon(obj, e);
}

void icon_as_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    click_auto_sleep_icon(obj, e);
}

void view_fl_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "top_view", SWITCH_OUT_NONE_ANIMATION,
                               SWITCH_IN_NONE_ANIMATION);
    }
    else if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "top_view", SWITCH_OUT_NONE_ANIMATION,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

/* @protected start custom_functions */
// Custom functions
void bg_circle_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    gui_obj_focus_set(GUI_BASE(obj)->parent);
}
void top_view_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    void *view_next = "easy_demoMainView";
    switch (mainface_idx)
    {
    case 1:
        view_next = "mainface_view_1";
        break;
    case 2:
        view_next = "mainface_view_2";
        break;
    case 3:
        view_next = "mainface_view_3";
        break;
    case 4:
        view_next = "mainface_view_4";
        break;
    case 5:
        view_next = "mainface_view_5";
        break;
    case 6:
        view_next = "mainface_view_6";
        break;
    case 7:
        view_next = "mainface_view_7";
        break;

    default:
        break;
    }
    gui_view_switch_on_event(obj, view_next, SWITCH_OUT_TO_TOP_USE_TRANSLATION, SWITCH_INIT_STATE,
                             GUI_EVENT_TOUCH_MOVE_UP);
}

void easy_demoMainView_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 0;
    gui_obj_focus_set(obj);
}

void mainface_view_1_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 1;
    gui_obj_focus_set(obj);
}

void mainface_view_2_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 2;
    gui_obj_focus_set(obj);
}

void mainface_view_3_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 3;
    gui_obj_focus_set(obj);
}

void mainface_view_4_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 4;
    gui_obj_focus_set(obj);
}

void mainface_view_5_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 5;
    gui_obj_focus_set(obj);
}

void mainface_view_6_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 6;
    gui_obj_focus_set(obj);
}

void mainface_view_7_update_idx_cb(void *obj)
{
    GUI_UNUSED(obj);
    mainface_idx = 7;
    gui_obj_focus_set(obj);
}
void easy_demoMainView_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 0);
    gui_obj_create_timer(obj, 20, true, easy_demoMainView_update_idx_cb);
    gui_obj_start_timer(obj);
}

void mainface_view_1_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 1);
    gui_obj_create_timer(obj, 20, true, mainface_view_1_update_idx_cb);
    gui_obj_start_timer(obj);
}

void mainface_view_2_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 2);
    gui_obj_create_timer(obj, 20, true, mainface_view_2_update_idx_cb);
    gui_obj_start_timer(obj);
}

void mainface_view_3_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 3);
    gui_obj_create_timer(obj, 20, true, mainface_view_3_update_idx_cb);
    gui_obj_start_timer(obj);
}

void mainface_view_4_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 4);
    gui_obj_create_timer(obj, 20, true, mainface_view_4_update_idx_cb);
    gui_obj_start_timer(obj);
}

void mainface_view_5_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 5);
    gui_obj_create_timer(obj, 20, true, mainface_view_5_update_idx_cb);
    gui_obj_start_timer(obj);
}

void mainface_view_6_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 6);
    gui_obj_create_timer(obj, 20, true, mainface_view_6_update_idx_cb);
    gui_obj_start_timer(obj);
}

void mainface_view_7_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    switch_mainface(obj, 7);
    gui_obj_create_timer(obj, 20, true, mainface_view_7_update_idx_cb);
    gui_obj_start_timer(obj);
}

void lbl_1_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    gui_obj_hidden(obj, !is_auto_sleep_mode);
    gui_obj_stop_timer(obj);
}

void icon_bat_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);

#ifdef _HONEYGUI_SIMULATOR_
    soc_val--;
    if (soc_val == 0) { soc_val = 100; }
#endif
    void *img_src = "/image/A8/bat_100_icon.bin";
    switch (soc_val / 20)
    {
    case 0:
        img_src = "/image/A8/bat_20_icon.bin";
        break;
    case 1:
        img_src = "/image/A8/bat_40_icon.bin";
        break;
    case 2:
        img_src = "/image/A8/bat_60_icon.bin";
        break;
    case 3:
        img_src = "/image/A8/bat_80_icon.bin";
        break;

    default:
        break;
    }
    gui_img_set_src(icon_bat, img_src, IMG_SRC_FILESYS);
}
/* @protected end custom_functions */
