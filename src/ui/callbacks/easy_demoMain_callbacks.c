#include "easy_demoMain_callbacks.h"
#include "../ui/easy_demoMain_ui.h"
#include "../user/easy_demoMain_user.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Timer animation counters
uint16_t easy_demoMainView_timer_cnt = 0;
uint16_t img_2_timer_cnt = 0;

// Event callback function implementations

// Preset timer callback functions

/**
 * 动画 1
 * Component: img_2
 * Mode: Preset actions (multi-segment animation)
 * Segments: 2
 */
void img_2_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 100;

    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 50;
    const uint16_t seg1_start = 50;
    const uint16_t seg1_end = 100;

    img_2_timer_cnt++;

    // Segment 1: 500ms, 1 action(s)
    if (img_2_timer_cnt > seg0_start && img_2_timer_cnt <= seg0_end)
    {
        uint16_t seg_cnt = img_2_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;

        // Adjust scale: (1, 1) -> (0.5, 0.5)
        const float zoom_x_origin = 1;
        const float zoom_x_target = 0.5;
        const float zoom_y_origin = 1;
        const float zoom_y_target = 0.5;
        float zoom_x_cur = zoom_x_origin + (zoom_x_target - zoom_x_origin) * seg_cnt / seg_cnt_max;
        float zoom_y_cur = zoom_y_origin + (zoom_y_target - zoom_y_origin) * seg_cnt / seg_cnt_max;
        gui_img_scale((gui_img_t *)target, zoom_x_cur, zoom_y_cur);

    }
    // Segment 2: 500ms, 1 action(s)
    else if (img_2_timer_cnt > seg1_start && img_2_timer_cnt <= seg1_end)
    {
        uint16_t seg_cnt = img_2_timer_cnt - seg1_start;
        const uint16_t seg_cnt_max = seg1_end - seg1_start;

        // Adjust scale: (0.5, 0.5) -> (1, 1)
        const float zoom_x_origin = 0.5;
        const float zoom_x_target = 1;
        const float zoom_y_origin = 0.5;
        const float zoom_y_target = 1;
        float zoom_x_cur = zoom_x_origin + (zoom_x_target - zoom_x_origin) * seg_cnt / seg_cnt_max;
        float zoom_y_cur = zoom_y_origin + (zoom_y_target - zoom_y_origin) * seg_cnt / seg_cnt_max;
        gui_img_scale((gui_img_t *)target, zoom_x_cur, zoom_y_cur);

    }

    if (img_2_timer_cnt >= total_cnt_max)
    {
        img_2_timer_cnt = 0; // Reset counter, continue loop
    }
}


/* @protected start custom_functions */
// Custom functions
#include "tp_algo.h"
uint8_t watchface_idx = 0;
uint8_t watchface_num = 3;
void *watchface_list[3] =
{
    "/wallpaper_1.mjpeg",
    "/wallpaper_2.mjpeg",
    "/wallpaper_3.mjpeg",
};
static int16_t y_rec = 0;
void switch_watchface(void *obj)
{
    gui_obj_tree_free(vid_1);
    vid_1 = gui_video_create_from_fs((gui_obj_t *)obj, "vid_1", watchface_list[watchface_idx], 0, 0,
                                     360, 360);
    gui_video_set_frame_rate((gui_video_t *)vid_1, 30.f);
    gui_video_set_repeat_count((gui_video_t *)vid_1, GUI_VIDEO_REPEAT_INFINITE);
    gui_video_set_state((gui_video_t *)vid_1, GUI_VIDEO_STATE_PLAYING);
}

void easy_demoMainView_timer_init_cb(void *obj)
{
    GUI_UNUSED(obj);
    y_rec = 0;
    switch_watchface(win_1);
    gui_obj_create_timer(obj, 10, true, easy_demoMainView_timer_0_cb);
    gui_obj_start_timer(obj);
}
void easy_demoMainView_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    touch_info_t *tp = tp_get_info();

    if (tp->pressing && tp->type == TOUCH_HOLD_Y)
    {
        int16_t y = y_rec + tp->deltaY;
        if (y < 0)
        {
            y = 0;
        }
        else if (y > 360)
        {
            y = 360;
        }
        gui_img_translate(img_1, 0, y);
        gui_log("img_1.y = %d\n", y);
    }
    else if (tp->type == TOUCH_LEFT_SLIDE_QUICK || tp->type == TOUCH_LEFT_SLIDE)
    {
        if (img_1->t_y > 0) { return; }
        watchface_idx++;
        watchface_idx %= watchface_num;
        switch_watchface(win_1);
    }
    else if (tp->type == TOUCH_RIGHT_SLIDE_QUICK || tp->type == TOUCH_RIGHT_SLIDE)
    {
        if (img_1->t_y > 0) { return; }
        watchface_idx += watchface_num;
        watchface_idx--;
        watchface_idx %= watchface_num;
        switch_watchface(win_1);
    }
    else if ((int16_t)(img_1->t_y) % 360 != 0)
    {
        int16_t y = (int16_t)img_1->t_y;
        const int16_t step = 20;
        if (y >= 180)
        {
            y += step;
        }
        else
        {
            y -= step;
        }
        if (y <= 0)
        {
            y = 0;
        }
        else if (y >= 360)
        {
            y = 360;
        }
        y_rec = y;
        gui_img_translate(img_1, 0, y);
        gui_log("img_1.y = %d\n", y);
    }
}
/* @protected end custom_functions */
