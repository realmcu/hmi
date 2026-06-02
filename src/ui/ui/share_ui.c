/**
 * share UI Implementation (Auto-generated, do not modify manually)
 * Generated at: 2026-06-02T11:57:21.538Z
 */
#include "share_ui.h"
#include "../callbacks/share_callbacks.h"
#include "../user/share_user.h"
#include <stddef.h>

// Component handle definitions
gui_img_t *img_4 = NULL;
gui_img_t *img_5 = NULL;
gui_text_t *lbl_2 = NULL;
gui_text_t *lbl_3 = NULL;
gui_img_t *img_6 = NULL;
gui_img_t *circle_anime = NULL;
gui_text_t *lbl_share = NULL;
gui_win_t *win_share_3 = NULL;
gui_img_t *img_8 = NULL;
gui_img_t *img_9 = NULL;


// Create shareMainView (hg_view)
static void shareMainView_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void shareMainView_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    GUI_UNUSED(view);


    // Create img_4 (hg_image)
    img_4 = gui_img_create_from_fs((gui_obj_t *)view, "img_4", "/image/A8/rect_268_70_bg.bin", 46, 108,
                                   268, 70);
    gui_img_set_mode((gui_img_t *)img_4, IMG_BYPASS_MODE);
    gui_obj_show((gui_obj_t *)img_4, true);
    gui_obj_add_event_cb(img_4, (gui_event_cb_t)img_4_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create img_5 (hg_image)
    img_5 = gui_img_create_from_fs((gui_obj_t *)view, "img_5", "/image/A8/rect_268_70_bg.bin", 46, 208,
                                   268, 70);
    gui_img_set_mode((gui_img_t *)img_5, IMG_BYPASS_MODE);
    gui_obj_show((gui_obj_t *)img_5, true);
    gui_obj_add_event_cb(img_5, (gui_event_cb_t)img_5_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create lbl_2 (hg_label)
    lbl_2 = gui_text_create((gui_obj_t *)view, "lbl_2", 107, 129, 146, 34);
    gui_text_set((gui_text_t *)lbl_2, "Share image", GUI_FONT_SRC_BMP, gui_rgb(0, 0, 0), 11, 24);
    gui_text_type_set((gui_text_t *)lbl_2, "/font/Inter_24pt_SemiBold_size24_bits4_bitmap.bin",
                      FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)lbl_2, CENTER);

    // Create lbl_3 (hg_label)
    lbl_3 = gui_text_create((gui_obj_t *)view, "lbl_3", 88, 230, 185, 34);
    gui_text_set((gui_text_t *)lbl_3, "Receive image", GUI_FONT_SRC_BMP, gui_rgb(0, 0, 0), 13, 24);
    gui_text_type_set((gui_text_t *)lbl_3, "/font/Inter_24pt_SemiBold_size24_bits4_bitmap.bin",
                      FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)lbl_3, CENTER);
}
GUI_VIEW_INSTANCE("shareMainView", false, shareMainView_switch_in, shareMainView_switch_out, false);

// Create shareSecondView (hg_view)
static void shareSecondView_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void shareSecondView_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    gui_view_switch_on_event(view, "shareMainView", SWITCH_INIT_STATE, SWITCH_IN_NONE_ANIMATION,
                             GUI_EVENT_TOUCH_LEFT_SLIDE_QUICK);
    gui_view_switch_on_event(view, "shareMainView", SWITCH_INIT_STATE, SWITCH_IN_NONE_ANIMATION,
                             GUI_EVENT_TOUCH_RIGHT_SLIDE_QUICK);


    // Create img_6 (hg_image)
    img_6 = gui_img_create_from_fs((gui_obj_t *)view, "img_6", "/image/A8/share_icon.bin", 135, 115, 90,
                                   90);
    gui_img_set_mode((gui_img_t *)img_6, IMG_BYPASS_MODE);
    gui_obj_show((gui_obj_t *)img_6, true);

    // Create circle_anime (hg_image)
    circle_anime = gui_img_create_from_fs((gui_obj_t *)view, "circle_anime", "/image/circle_anime.bin",
                                          44, 44, 272, 272);
    gui_img_set_quality((gui_img_t *)circle_anime, true);
    gui_img_translate((gui_img_t *)circle_anime, 133.0f, 133.0f);
    gui_img_set_focus((gui_img_t *)circle_anime, 133.0f, 133.0f);
    gui_img_rotation((gui_img_t *)circle_anime, -45.0f);
    gui_obj_show((gui_obj_t *)circle_anime, true);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)circle_anime, 10, true, circle_anime_timer_0_cb);

    // Create lbl_share (hg_label)
    lbl_share = gui_text_create((gui_obj_t *)view, "lbl_share", 109, 222, 142, 24);
    gui_text_set((gui_text_t *)lbl_share, "Share image", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 11,
                 18);
    gui_text_type_set((gui_text_t *)lbl_share, "/font/Inter_24pt_SemiBold_size18_bits4_bitmap.bin",
                      FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)lbl_share, CENTER);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)lbl_share, 1000, true, lbl_share_timer_0_cb);

    // Create win_share_3 (hg_window)
    win_share_3 = gui_win_create((gui_obj_t *)view, "win_share_3", 0, 0, 360, 360);
    gui_obj_show((gui_obj_t *)win_share_3, false);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)win_share_3, 10, true, win_share_3_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)win_share_3);


    // Create img_8 (hg_image)
    img_8 = gui_img_create_from_fs(win_share_3, "img_8", "/image/A8/circle_360_bg.bin", 0, 0, 360, 360);
    gui_img_set_mode((gui_img_t *)img_8, IMG_SRC_OVER_MODE);
    gui_img_translate((gui_img_t *)img_8, 180.0f, 180.0f);
    gui_img_set_focus((gui_img_t *)img_8, 180.0f, 180.0f);
    gui_img_scale((gui_img_t *)img_8, 0.620000f, 0.620000f);
    gui_img_set_opacity((gui_img_t *)img_8, 122);
    gui_obj_show((gui_obj_t *)img_8, true);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)img_8, 1000, true, img_8_timer_0_cb);

    // Create img_9 (hg_image)
    img_9 = gui_img_create_from_fs(win_share_3, "img_9", "/image/A8/connect_state_icon.bin", 142, 132,
                                   76, 76);
    gui_obj_show((gui_obj_t *)img_9, true);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)shareSecondView_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("shareSecondView", false, shareSecondView_switch_in, shareSecondView_switch_out,
                  false);
