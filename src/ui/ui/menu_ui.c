/**
 * menu UI Implementation (Auto-generated, do not modify manually)
 * Generated at: 2026-06-04T02:36:21.687Z
 */
#include "menu_ui.h"
#include "../callbacks/menu_callbacks.h"
#include "../user/menu_user.h"
#include <stddef.h>

// Component handle definitions
gui_list_t *lst_1 = NULL;
gui_img_t *img_10 = NULL;
gui_img_t *img_11 = NULL;
gui_text_t *lbl_6 = NULL;
gui_img_t *img_12 = NULL;
gui_img_t *img_14 = NULL;
gui_text_t *lbl_7 = NULL;
gui_img_t *img_13 = NULL;
gui_img_t *img_15 = NULL;
gui_text_t *lbl_8 = NULL;
gui_text_t *lbl_option = NULL;
gui_img_t *img_16 = NULL;
gui_img_t *img_17 = NULL;
gui_text_t *lbl_9 = NULL;
gui_text_t *lbl_10 = NULL;

// List component note_design callback functions
// note_design callback function declaration
static void lst_1_note_design(gui_obj_t *obj, void *param);

// note_design callback function implementation
static void lst_1_note_design(gui_obj_t *obj, void *param)
{
    GUI_UNUSED(param);

    // Cast obj to gui_list_note_t * type
    gui_list_note_t *note = (gui_list_note_t *)obj;
    uint16_t index = note->index;

    // Create different list_item content based on index
    switch (index)
    {
    case 0:
        {
            // Create img_10 (hg_image)
            img_10 = gui_img_create_from_fs((gui_obj_t *)note, "img_10", "/image/reset_icon.bin", 50, 0, 50,
                                            50);
            gui_obj_show((gui_obj_t *)img_10, true);
            // Create img_11 (hg_image)
            img_11 = gui_img_create_from_fs((gui_obj_t *)note, "img_11", "/image/A8/arrow_r_icon.bin", 284, 15,
                                            11, 21);
            gui_img_set_mode((gui_img_t *)img_11, IMG_BYPASS_MODE);
            gui_obj_show((gui_obj_t *)img_11, true);
            // Create lbl_6 (hg_label)
            lbl_6 = gui_text_create((gui_obj_t *)note, "lbl_6", 117, 0, 125, 50);
            gui_text_set((gui_text_t *)lbl_6, "Reset", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 5, 20);
            gui_text_type_set((gui_text_t *)lbl_6, "/font/Inter_24pt_SemiBold_size20_bits4_bitmap.bin",
                              FONT_SRC_FILESYS);
            gui_text_mode_set((gui_text_t *)lbl_6, MID_LEFT);
            gui_obj_add_event_cb(obj, click_menu_reset_button, GUI_EVENT_TOUCH_CLICKED, NULL);
            break;
        }
    case 1:
        {
            // Create img_12 (hg_image)
            img_12 = gui_img_create_from_fs((gui_obj_t *)note, "img_12", "/image/A8/arrow_r_icon.bin", 284, 15,
                                            11, 21);
            gui_img_set_mode((gui_img_t *)img_12, IMG_BYPASS_MODE);
            gui_obj_show((gui_obj_t *)img_12, true);
            // Create img_14 (hg_image)
            img_14 = gui_img_create_from_fs((gui_obj_t *)note, "img_14", "/image/erase_icon.bin", 50, 0, 50,
                                            50);
            gui_obj_show((gui_obj_t *)img_14, true);
            // Create lbl_7 (hg_label)
            lbl_7 = gui_text_create((gui_obj_t *)note, "lbl_7", 117, 0, 160, 50);
            gui_text_set((gui_text_t *)lbl_7, "Format Storage", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 14,
                         20);
            gui_text_type_set((gui_text_t *)lbl_7, "/font/Inter_24pt_SemiBold_size20_bits4_bitmap.bin",
                              FONT_SRC_FILESYS);
            gui_text_mode_set((gui_text_t *)lbl_7, MID_LEFT);
            gui_obj_add_event_cb(obj, click_menu_fs_button, GUI_EVENT_TOUCH_CLICKED, NULL);
            break;
        }
    case 2:
        {
            // Create img_13 (hg_image)
            img_13 = gui_img_create_from_fs((gui_obj_t *)note, "img_13", "/image/A8/arrow_r_icon.bin", 284, 15,
                                            11, 21);
            gui_img_set_mode((gui_img_t *)img_13, IMG_BYPASS_MODE);
            gui_obj_show((gui_obj_t *)img_13, true);
            // Create img_15 (hg_image)
            img_15 = gui_img_create_from_fs((gui_obj_t *)note, "img_15", "/image/power_off_icon.bin", 50, 0, 50,
                                            50);
            gui_obj_show((gui_obj_t *)img_15, true);
            // Create lbl_8 (hg_label)
            lbl_8 = gui_text_create((gui_obj_t *)note, "lbl_8", 117, 0, 160, 50);
            gui_text_set((gui_text_t *)lbl_8, "Power off", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 9, 20);
            gui_text_type_set((gui_text_t *)lbl_8, "/font/Inter_24pt_SemiBold_size20_bits4_bitmap.bin",
                              FONT_SRC_FILESYS);
            gui_text_mode_set((gui_text_t *)lbl_8, MID_LEFT);
            gui_obj_add_event_cb(obj, click_menu_power_off_button, GUI_EVENT_TOUCH_CLICKED, NULL);
            break;
        }
    default:
        break;
    }
}


// Create menuMainView (hg_view)
static void menuMainView_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void menuMainView_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    GUI_UNUSED(view);


    // Create lst_1 (hg_list)
    lst_1 = gui_list_create((gui_obj_t *)view, "lst_1", 0, 77, 360, 250, 50, 28, VERTICAL,
                            lst_1_note_design, NULL, false);
    gui_list_set_style(lst_1, LIST_CLASSIC);
    gui_list_set_note_num(lst_1, 3);
    gui_list_set_inertia(lst_1, false);
    gui_obj_show((gui_obj_t *)lst_1, true);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)menuMainView_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("menuMainView", false, menuMainView_switch_in, menuMainView_switch_out, false);

// Create menuDetailView (hg_view)
static void menuDetailView_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void menuDetailView_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 360);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    GUI_UNUSED(view);


    // Create lbl_option (hg_label)
    lbl_option = gui_text_create((gui_obj_t *)view, "lbl_option", 0, 150, 360, 52);
    gui_text_set((gui_text_t *)lbl_option, "Reset", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 5, 36);
    gui_text_type_set((gui_text_t *)lbl_option, "/font/Inter_24pt_Regular_size36_bits4_bitmap.bin",
                      FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)lbl_option, MID_CENTER);
    // Bind timer: 动画 1
    gui_obj_create_timer((gui_obj_t *)lbl_option, 1000, true, lbl_option_timer_0_cb);

    // Create img_16 (hg_image)
    img_16 = gui_img_create_from_fs((gui_obj_t *)view, "img_16", "/image/A8/rect_bg_red.bin", 217, 223,
                                    94, 48);
    gui_img_set_mode((gui_img_t *)img_16, IMG_BYPASS_MODE);
    gui_obj_show((gui_obj_t *)img_16, true);
    gui_obj_add_event_cb(img_16, (gui_event_cb_t)img_16_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create img_17 (hg_image)
    img_17 = gui_img_create_from_fs((gui_obj_t *)view, "img_17", "/image/A8/rect_bg_grey.bin", 53, 223,
                                    94, 48);
    gui_img_set_mode((gui_img_t *)img_17, IMG_BYPASS_MODE);
    gui_obj_show((gui_obj_t *)img_17, true);
    gui_obj_add_event_cb(img_17, (gui_event_cb_t)img_17_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create lbl_9 (hg_label)
    lbl_9 = gui_text_create((gui_obj_t *)view, "lbl_9", 53, 223, 94, 48);
    gui_text_set((gui_text_t *)lbl_9, "No", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 2, 20);
    gui_text_type_set((gui_text_t *)lbl_9, "/font/Inter_24pt_Regular_size20_bits4_bitmap.bin",
                      FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)lbl_9, MID_CENTER);

    // Create lbl_10 (hg_label)
    lbl_10 = gui_text_create((gui_obj_t *)view, "lbl_10", 217, 223, 94, 48);
    gui_text_set((gui_text_t *)lbl_10, "Yes", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 3, 20);
    gui_text_type_set((gui_text_t *)lbl_10, "/font/Inter_24pt_Regular_size20_bits4_bitmap.bin",
                      FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)lbl_10, MID_CENTER);

    gui_obj_add_event_cb((gui_obj_t *)view, (gui_event_cb_t)menuDetailView_key_0_cb,
                         GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)view);
}
GUI_VIEW_INSTANCE("menuDetailView", false, menuDetailView_switch_in, menuDetailView_switch_out,
                  false);
