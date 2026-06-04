/**
 * easy_demoMain UI Implementation (Auto-generated, do not modify manually)
 * Generated at: 2026-06-04T02:36:21.499Z
 */
#include "easy_demoMain_ui.h"
#include "../callbacks/easy_demoMain_callbacks.h"
#include "../user/easy_demoMain_user.h"
#include <stddef.h>

// Component handle definitions
gui_img_t *bg_circle = NULL;
gui_img_t *icon_del = NULL;
gui_img_t *icon_fl = NULL;
gui_img_t *icon_connect = NULL;
gui_img_t *icon_bat = NULL;
gui_img_t *icon_sl = NULL;
gui_img_t *icon_as = NULL;
gui_text_t *lbl_1 = NULL;
gui_lite_video_t *vid_test = NULL;
gui_img_t *img_3 = NULL;


// Create easy_demoMainView (hg_view)
static void easy_demoMainView_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void easy_demoMainView_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, easy_demoMainView_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)easy_demoMainView_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)easy_demoMainView_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("easy_demoMainView", false, easy_demoMainView_switch_in,
                  easy_demoMainView_switch_out, false);

// Create mainface_view_1 (hg_view)
static void mainface_view_1_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void mainface_view_1_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, mainface_view_1_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_1_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_1_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("mainface_view_1", false, mainface_view_1_switch_in, mainface_view_1_switch_out,
                  false);

// Create mainface_view_2 (hg_view)
static void mainface_view_2_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void mainface_view_2_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, mainface_view_2_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_2_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_2_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("mainface_view_2", false, mainface_view_2_switch_in, mainface_view_2_switch_out,
                  false);

// Create mainface_view_3 (hg_view)
static void mainface_view_3_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void mainface_view_3_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, mainface_view_3_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_3_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_3_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("mainface_view_3", false, mainface_view_3_switch_in, mainface_view_3_switch_out,
                  false);

// Create mainface_view_4 (hg_view)
static void mainface_view_4_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void mainface_view_4_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, mainface_view_4_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_4_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_4_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("mainface_view_4", false, mainface_view_4_switch_in, mainface_view_4_switch_out,
                  false);

// Create mainface_view_5 (hg_view)
static void mainface_view_5_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void mainface_view_5_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, mainface_view_5_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_5_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_5_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("mainface_view_5", false, mainface_view_5_switch_in, mainface_view_5_switch_out,
                  false);

// Create mainface_view_6 (hg_view)
static void mainface_view_6_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void mainface_view_6_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, mainface_view_6_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_6_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_6_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("mainface_view_6", false, mainface_view_6_switch_in, mainface_view_6_switch_out,
                  false);

// Create mainface_view_7 (hg_view)
static void mainface_view_7_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void mainface_view_7_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, mainface_view_7_timer_0_cb);

    GUI_UNUSED(view);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_7_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)mainface_view_7_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("mainface_view_7", false, mainface_view_7_switch_in, mainface_view_7_switch_out,
                  false);

// Create top_view (hg_view)
static void top_view_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void top_view_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)view, 1000, true, top_view_timer_0_cb);

    GUI_UNUSED(view);


    // Create bg_circle (hg_image)
    bg_circle = gui_img_create_from_fs((gui_obj_t *)view, "bg_circle", "/image/A8/circle_360_bg.bin", 0,
                                       0, 360, 360);
    gui_img_set_mode((gui_img_t *)bg_circle, IMG_SRC_OVER_MODE);
    gui_img_set_opacity((gui_img_t *)bg_circle, 100);
    gui_obj_show((gui_obj_t *)bg_circle, true);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)bg_circle, 20, true, bg_circle_timer_0_cb);

    // Create icon_del (hg_image)
    icon_del = gui_img_create_from_fs((gui_obj_t *)view, "icon_del", "/image/A8/delete_icon.bin", 244,
                                      109, 100, 100);
    gui_img_set_mode((gui_img_t *)icon_del, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)icon_del, true);
    gui_obj_add_event_cb(icon_del, (gui_event_cb_t)icon_del_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create icon_fl (hg_image)
    icon_fl = gui_img_create_from_fs((gui_obj_t *)view, "icon_fl", "/image/A8/flashlight_icon.bin", 17,
                                     109, 100, 100);
    gui_img_set_mode((gui_img_t *)icon_fl, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)icon_fl, true);
    gui_obj_add_event_cb(icon_fl, (gui_event_cb_t)icon_fl_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create icon_connect (hg_image)
    icon_connect = gui_img_create_from_fs((gui_obj_t *)view, "icon_connect",
                                          "/image/A8/connect_icon.bin", 194, 225, 100, 100);
    gui_img_set_mode((gui_img_t *)icon_connect, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)icon_connect, true);
    gui_obj_add_event_cb(icon_connect, (gui_event_cb_t)icon_connect_clicked_cb, GUI_EVENT_TOUCH_CLICKED,
                         NULL);

    // Create icon_bat (hg_image)
    icon_bat = gui_img_create_from_fs((gui_obj_t *)view, "icon_bat", "/image/A8/bat_100_icon.bin", 151,
                                      142, 58, 34);
    gui_img_set_mode((gui_img_t *)icon_bat, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)icon_bat, true);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)icon_bat, 1000, true, icon_bat_timer_0_cb);

    // Create icon_sl (hg_image)
    icon_sl = gui_img_create_from_fs((gui_obj_t *)view, "icon_sl", "/image/screen_light_6_icon.bin", 67,
                                     225, 100, 100);
    gui_img_set_mode((gui_img_t *)icon_sl, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)icon_sl, true);
    gui_obj_add_event_cb(icon_sl, (gui_event_cb_t)icon_sl_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create icon_as (hg_image)
    icon_as = gui_img_create_from_fs((gui_obj_t *)view, "icon_as", "/image/auto_sleep_off_icon.bin",
                                     130, 9, 100, 100);
    gui_img_set_mode((gui_img_t *)icon_as, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)icon_as, true);
    gui_obj_add_event_cb(icon_as, (gui_event_cb_t)icon_as_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create lbl_1 (hg_label)
    lbl_1 = gui_text_create((gui_obj_t *)view, "lbl_1", 111, 194, 144, 24);
    gui_text_set((gui_text_t *)lbl_1, "Auto sleep mode", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 15,
                 18);
    gui_text_type_set((gui_text_t *)lbl_1, "/font/Inter_24pt_SemiBold_size18_bits4_bitmap.bin",
                      FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)lbl_1, LEFT);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)lbl_1, 10, false, lbl_1_timer_0_cb);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)top_view_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)top_view_key_1_cb,
                         GUI_EVENT_KB_LONG_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("top_view", false, top_view_switch_in, top_view_switch_out, false);

// Create view_1 (hg_view)
static void view_1_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void view_1_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 36);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    GUI_UNUSED(view);


    // Create vid_test (hg_video)
    vid_test = gui_lite_video_create_from_fs((gui_obj_t *)view, "vid_test", "/wallpaper_1.avi", 0, 0,
                                             365, 360);
    gui_lite_video_set_frame_rate((gui_lite_video_t *)vid_test, 35.f);
    gui_lite_video_set_repeat_count((gui_lite_video_t *)vid_test, GUI_VIDEO_REPEAT_INFINITE);
    gui_lite_video_set_state((gui_lite_video_t *)vid_test, GUI_VIDEO_STATE_PLAYING);
    gui_obj_show((gui_obj_t *)vid_test, true);
}
GUI_VIEW_INSTANCE("view_1", false, view_1_switch_in, view_1_switch_out, false);

// Create view_fl (hg_view)
static void view_fl_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void view_fl_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    GUI_UNUSED(view);


    // Create img_3 (hg_image)
    img_3 = gui_img_create_from_fs((gui_obj_t *)view, "img_3", "/image/A8/circle_360_bg_white.bin", 0,
                                   0, 360, 360);
    gui_img_set_mode((gui_img_t *)img_3, IMG_BYPASS_MODE);
    gui_obj_show((gui_obj_t *)img_3, true);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)view_fl_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("view_fl", false, view_fl_switch_in, view_fl_switch_out, false);
