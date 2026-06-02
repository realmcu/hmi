#include "share_callbacks.h"
#include "../ui/share_ui.h"
#include "../user/share_user.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Timer animation counters
uint16_t circle_anime_timer_cnt = 0;
uint16_t lbl_share_timer_cnt = 0;
uint16_t win_share_3_timer_cnt = 0;
uint16_t img_8_timer_cnt = 0;

// Event callback function implementations

void img_4_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    click_share_image_button(obj, e);
    gui_view_switch_direct(gui_view_get_current(), "shareSecondView", SWITCH_INIT_STATE,
                           SWITCH_IN_NONE_ANIMATION);
}

void img_5_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    click_receive_image_button(obj, e);
    gui_view_switch_direct(gui_view_get_current(), "shareSecondView", SWITCH_INIT_STATE,
                           SWITCH_IN_NONE_ANIMATION);
}

void shareSecondView_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "shareMainView", SWITCH_OUT_NONE_ANIMATION,
                               SWITCH_IN_NONE_ANIMATION);
    }
    else if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "shareMainView", SWITCH_OUT_NONE_ANIMATION,
                               SWITCH_IN_NONE_ANIMATION);
    }
}

// Preset timer callback functions

/**
 * 动画 1
 * Component: circle_anime
 * Mode: Preset actions (multi-segment animation)
 * Segments: 2
 */
void circle_anime_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 40;

    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 20;
    const uint16_t seg1_start = 20;
    const uint16_t seg1_end = 40;

    circle_anime_timer_cnt++;

    // Segment 1: 200ms, 1 action(s)
    if (circle_anime_timer_cnt > seg0_start && circle_anime_timer_cnt <= seg0_end)
    {
        uint16_t seg_cnt = circle_anime_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;

        // Adjust rotation: -45° -> 45°
        const float angle_origin = -45;
        const float angle_target = 45;
        float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
        gui_img_rotation((gui_img_t *)target, angle_cur);

    }
    // Segment 2: 200ms, 1 action(s)
    else if (circle_anime_timer_cnt > seg1_start && circle_anime_timer_cnt <= seg1_end)
    {
        uint16_t seg_cnt = circle_anime_timer_cnt - seg1_start;
        const uint16_t seg_cnt_max = seg1_end - seg1_start;

        // Adjust rotation: 45° -> -45°
        const float angle_origin = 45;
        const float angle_target = -45;
        float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
        gui_img_rotation((gui_img_t *)target, angle_cur);

    }

    if (circle_anime_timer_cnt >= total_cnt_max)
    {
        circle_anime_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * 动画 1
 * Component: img_8
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void img_8_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;

    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;

    img_8_timer_cnt++;

    // Segment 1: 1000ms, 1 action(s)
    if (img_8_timer_cnt > seg0_start && img_8_timer_cnt <= seg0_end)
    {
        uint16_t seg_cnt = img_8_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;

        // Adjust scale: (1, 1) -> (0.62, 0.62)
        const float zoom_x_origin = 1;
        const float zoom_x_target = 0.62;
        const float zoom_y_origin = 1;
        const float zoom_y_target = 0.62;
        float zoom_x_cur = zoom_x_origin + (zoom_x_target - zoom_x_origin) * seg_cnt / seg_cnt_max;
        float zoom_y_cur = zoom_y_origin + (zoom_y_target - zoom_y_origin) * seg_cnt / seg_cnt_max;
        gui_img_scale((gui_img_t *)target, zoom_x_cur, zoom_y_cur);

    }

    if (img_8_timer_cnt >= total_cnt_max)
    {
        gui_obj_stop_timer(target);
        img_8_timer_cnt = 0; // Reset counter
    }
}


/* @protected start custom_functions */
// Custom functions
void lbl_share_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    if (dev_mode == MODE_RECEIVE) { gui_text_content_set(lbl_share, "Receive Image", strlen("Receive Image")); }
    gui_obj_stop_timer(obj);
}

void win_share_3_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
#ifdef _HONEYGUI_SIMULATOR_
    win_share_3_timer_cnt++;
    if (win_share_3_timer_cnt >= 500)
    {
        win_share_3_timer_cnt = 0;
        dev_mode = MODE_DEFAULT;
    }
#endif
    if (dev_mode == MODE_DEFAULT)
    {
        gui_obj_hidden(obj, false);
        gui_text_content_set(lbl_share, "Time out", strlen("Time out"));
        gui_obj_stop_timer(obj);
        gui_obj_stop_timer(GUI_BASE(circle_anime));
    }
}
/* @protected end custom_functions */
