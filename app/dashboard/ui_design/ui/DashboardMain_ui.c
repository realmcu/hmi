/**
 * DashboardMain UI Implementation (Auto-generated, do not modify manually)
 * Generated at: 2026-05-08T07:44:30.011Z
 */
#include "DashboardMain_ui.h"
#include "../callbacks/DashboardMain_callbacks.h"
#include "../user/DashboardMain_user.h"
#include <stddef.h>

// Component handle definitions
gui_img_t *gloom = NULL;
gui_img_t *start_engine = NULL;
gui_img_t *shadow = NULL;
gui_win_t *win_map = NULL;
gui_img_t *icon_dir = NULL;
gui_img_t *map = NULL;
gui_img_t *root_menu = NULL;
gui_win_t *win_dashboard = NULL;
gui_win_t *win_line = NULL;
gui_img_t *line_left = NULL;
gui_img_t *line_right = NULL;
gui_win_t *win_dail = NULL;
gui_win_t *win_speed = NULL;
gui_img_t *dail_speed = NULL;
gui_img_t *speed_arc = NULL;
gui_img_t *dail_pointer_s = NULL;
gui_win_t *win_power = NULL;
gui_img_t *dail_power = NULL;
gui_img_t *power_arc = NULL;
gui_img_t *dail_pointer_p = NULL;
gui_img_t *rightbottom_gloom = NULL;
gui_img_t *leftbottom_gloom = NULL;
gui_win_t *win_common = NULL;
gui_img_t *turn_l_icon = NULL;
gui_img_t *turn_r_icon = NULL;
gui_img_t *off_icon = NULL;
gui_img_t *bt_icon = NULL;
gui_img_t *wifi_icon = NULL;
gui_text_t *text_loc_temp = NULL;
gui_text_t *text_time = NULL;
gui_img_t *led_0 = NULL;
gui_img_t *led_1 = NULL;
gui_img_t *led_2 = NULL;
gui_img_t *led_3 = NULL;
gui_img_t *led_4 = NULL;
gui_img_t *led_5 = NULL;
gui_win_t *win_speed_text = NULL;
gui_text_t *text_speed = NULL;
gui_text_t *text_speed_unit = NULL;
gui_img_t *line = NULL;
gui_text_t *text_odo = NULL;
gui_win_t *win_power_text = NULL;
gui_text_t *text_power = NULL;
gui_text_t *text_power_unit = NULL;
gui_img_t *line_copy_1768894235876_3 = NULL;
gui_text_t *text_battery = NULL;
gui_win_t *win_func = NULL;
gui_win_t *win_app_list = NULL;
gui_list_t *app_list = NULL;
gui_text_t *text_app_name_0 = NULL;
gui_text_t *text_app_name_1 = NULL;
gui_text_t *text_app_name_2 = NULL;
gui_img_t *interval_line_1 = NULL;
gui_img_t *interval_line_2 = NULL;
gui_img_t *arrow_down = NULL;
gui_text_t *text_app_name_3 = NULL;
gui_text_t *text_app_name_4 = NULL;
gui_text_t *text_app_name_5 = NULL;
gui_img_t *interval_line_3 = NULL;
gui_img_t *interval_line_4 = NULL;
gui_img_t *arrow_up = NULL;
gui_img_t *tab_hightlight = NULL;
gui_win_t *win_app_weather = NULL;
gui_list_t *list_weather = NULL;
gui_img_t *weather_1 = NULL;
gui_img_t *weather_2 = NULL;
gui_win_t *win_app_developing = NULL;
gui_text_t *text_app_developing = NULL;

// List component note_design callback functions
// note_design callback function declaration
static void app_list_note_design(gui_obj_t *obj, void *param);

// note_design callback function implementation
static void app_list_note_design(gui_obj_t *obj, void *param)
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
        // Create text_app_name_0 (hg_label)
        text_app_name_0 = gui_text_create((gui_obj_t *)note, "text_app_name_0", 60, 0, 360, 90);
        gui_text_set((gui_text_t *)text_app_name_0, "Navigation", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 10, 30);
        gui_text_type_set((gui_text_t *)text_app_name_0, "//HarmonyOS_Sans_Bold_size30_bits4_bitmap.bin", FONT_SRC_FILESYS);
        gui_text_mode_set((gui_text_t *)text_app_name_0, MID_LEFT);
        // Create text_app_name_1 (hg_label)
        text_app_name_1 = gui_text_create((gui_obj_t *)note, "text_app_name_1", 60, 90, 360, 90);
        gui_text_set((gui_text_t *)text_app_name_1, "Music", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 5, 30);
        gui_text_type_set((gui_text_t *)text_app_name_1, "//HarmonyOS_Sans_Bold_size30_bits4_bitmap.bin", FONT_SRC_FILESYS);
        gui_text_mode_set((gui_text_t *)text_app_name_1, MID_LEFT);
        // Create text_app_name_2 (hg_label)
        text_app_name_2 = gui_text_create((gui_obj_t *)note, "text_app_name_2", 60, 180, 360, 90);
        gui_text_set((gui_text_t *)text_app_name_2, "Call", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 4, 30);
        gui_text_type_set((gui_text_t *)text_app_name_2, "//HarmonyOS_Sans_Bold_size30_bits4_bitmap.bin", FONT_SRC_FILESYS);
        gui_text_mode_set((gui_text_t *)text_app_name_2, MID_LEFT);
        // Create interval_line_1 (hg_image)
        interval_line_1 = gui_img_create_from_fs((gui_obj_t *)note, "interval_line_1", "/resource/func/interval_line.bin", 40, 90, 420, 3);
        gui_img_set_mode((gui_img_t *)interval_line_1, IMG_2D_SW_FIX_A8_FG);
        gui_img_a8_recolor((gui_img_t *)interval_line_1, 0xFFFFFFFF);
        gui_obj_show((gui_obj_t *)interval_line_1, true);
        // Create interval_line_2 (hg_image)
        interval_line_2 = gui_img_create_from_fs((gui_obj_t *)note, "interval_line_2", "/resource/func/interval_line.bin", 40, 180, 420, 3);
        gui_img_set_mode((gui_img_t *)interval_line_2, IMG_2D_SW_FIX_A8_FG);
        gui_img_a8_recolor((gui_img_t *)interval_line_2, 0xFFFFFFFF);
        gui_obj_show((gui_obj_t *)interval_line_2, true);
        // Create arrow_down (hg_image)
        arrow_down = gui_img_create_from_fs((gui_obj_t *)note, "arrow_down", "/resource/func/arrow_down.bin", 460, 244, 31, 16);
        gui_img_set_mode((gui_img_t *)arrow_down, IMG_2D_SW_FIX_A8_FG);
        gui_img_a8_recolor((gui_img_t *)arrow_down, 0xFFFFFFFF);
        gui_obj_show((gui_obj_t *)arrow_down, true);
        break;
    }
    case 1:
    {
        // Create text_app_name_3 (hg_label)
        text_app_name_3 = gui_text_create((gui_obj_t *)note, "text_app_name_3", 60, 0, 360, 90);
        gui_text_set((gui_text_t *)text_app_name_3, "Weather", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 7, 30);
        gui_text_type_set((gui_text_t *)text_app_name_3, "//HarmonyOS_Sans_Bold_size30_bits4_bitmap.bin", FONT_SRC_FILESYS);
        gui_text_mode_set((gui_text_t *)text_app_name_3, MID_LEFT);
        // Create text_app_name_4 (hg_label)
        text_app_name_4 = gui_text_create((gui_obj_t *)note, "text_app_name_4", 60, 90, 360, 90);
        gui_text_set((gui_text_t *)text_app_name_4, "Notification", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 12, 30);
        gui_text_type_set((gui_text_t *)text_app_name_4, "//HarmonyOS_Sans_Bold_size30_bits4_bitmap.bin", FONT_SRC_FILESYS);
        gui_text_mode_set((gui_text_t *)text_app_name_4, MID_LEFT);
        // Create text_app_name_5 (hg_label)
        text_app_name_5 = gui_text_create((gui_obj_t *)note, "text_app_name_5", 60, 180, 360, 90);
        gui_text_set((gui_text_t *)text_app_name_5, "Information Transfer", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 20, 30);
        gui_text_type_set((gui_text_t *)text_app_name_5, "//HarmonyOS_Sans_Bold_size30_bits4_bitmap.bin", FONT_SRC_FILESYS);
        gui_text_mode_set((gui_text_t *)text_app_name_5, MID_LEFT);
        // Create interval_line_3 (hg_image)
        interval_line_3 = gui_img_create_from_fs((gui_obj_t *)note, "interval_line_3", "/resource/func/interval_line.bin", 40, 90, 420, 3);
        gui_img_set_mode((gui_img_t *)interval_line_3, IMG_2D_SW_FIX_A8_FG);
        gui_img_a8_recolor((gui_img_t *)interval_line_3, 0xFFFFFFFF);
        gui_obj_show((gui_obj_t *)interval_line_3, true);
        // Create interval_line_4 (hg_image)
        interval_line_4 = gui_img_create_from_fs((gui_obj_t *)note, "interval_line_4", "/resource/func/interval_line.bin", 40, 180, 420, 3);
        gui_img_set_mode((gui_img_t *)interval_line_4, IMG_2D_SW_FIX_A8_FG);
        gui_img_a8_recolor((gui_img_t *)interval_line_4, 0xFFFFFFFF);
        gui_obj_show((gui_obj_t *)interval_line_4, true);
        // Create arrow_up (hg_image)
        arrow_up = gui_img_create_from_fs((gui_obj_t *)note, "arrow_up", "/resource/func/arrow_up.bin", 460, 22, 31, 16);
        gui_img_set_mode((gui_img_t *)arrow_up, IMG_2D_SW_FIX_A8_FG);
        gui_img_a8_recolor((gui_img_t *)arrow_up, 0xFFFFFFFF);
        gui_obj_show((gui_obj_t *)arrow_up, true);
        break;
    }
    default:
        break;
    }
}

// note_design callback function declaration
static void list_weather_note_design(gui_obj_t *obj, void *param);

// note_design callback function implementation
static void list_weather_note_design(gui_obj_t *obj, void *param)
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
        // Create weather_1 (hg_image)
        weather_1 = gui_img_create_from_fs((gui_obj_t *)note, "weather_1", "/resource/func/app_weather_1.bin", 0, 0, 500, 280);
        gui_obj_show((gui_obj_t *)weather_1, true);
        break;
    }
    case 1:
    {
        // Create weather_2 (hg_image)
        weather_2 = gui_img_create_from_fs((gui_obj_t *)note, "weather_2", "/resource/func/app_weather_2.bin", 0, 0, 500, 280);
        gui_obj_show((gui_obj_t *)weather_2, true);
        break;
    }
    default:
        break;
    }
}


// Create dashboardMainView (hg_view)
static void dashboardMainView_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void dashboardMainView_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 500);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    GUI_UNUSED(view);


    // Create gloom (hg_image)
    gloom = gui_img_create_from_fs((gui_obj_t *)view, "gloom", "/resource/StartEngineGloom.bin", 80, 58, 640, 364);
    gui_img_set_mode((gui_img_t *)gloom, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)gloom, 0xFF01BFFE);
    gui_img_set_opacity((gui_img_t *)gloom, 0);
    gui_obj_show((gui_obj_t *)gloom, true);

    // Create start_engine (hg_image)
    start_engine = gui_img_create_from_fs((gui_obj_t *)view, "start_engine", "/resource/StartEngine.bin", 305, 145, 190, 190);
    gui_obj_show((gui_obj_t *)start_engine, true);
    gui_obj_add_event_cb(start_engine, (gui_event_cb_t)start_engine_pressed_cb, GUI_EVENT_TOUCH_PRESSED, NULL);
    gui_obj_add_event_cb(start_engine, (gui_event_cb_t)start_engine_released_cb, GUI_EVENT_TOUCH_RELEASED, NULL);
    gui_obj_add_event_cb(start_engine, (gui_event_cb_t)start_engine_key_cb, GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)start_engine);

    // Create shadow (hg_image)
    shadow = gui_img_create_from_fs((gui_obj_t *)view, "shadow", "/resource/Start_Engine_Release.bin", 348, 187, 105, 105);
    gui_img_set_mode((gui_img_t *)shadow, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)shadow, 0xFF01b8f6);
    gui_obj_show((gui_obj_t *)shadow, true);
}
GUI_VIEW_INSTANCE("dashboardMainView", false, dashboardMainView_switch_in, dashboardMainView_switch_out);

// Create dashboard_view (hg_view)
static void dashboard_view_switch_out(gui_view_t *view)
{
    GUI_UNUSED(view);
}

static void dashboard_view_switch_in(gui_view_t *view)
{
    // Set animation step
    gui_view_set_animate_step(view, 500);

    // Set opacity
    gui_view_set_opacity(view, 255);

    // Set background color
    gui_set_bg_color(gui_rgb(0, 0, 0));

    GUI_UNUSED(view);


    // Create win_map (hg_window)
    win_map = gui_win_create((gui_obj_t *)view, "win_map", 0, 0, 800, 480);
    gui_obj_show((gui_obj_t *)win_map, false);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)win_map, 6000, false, win_map_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)win_map);


    // Create map (hg_image)
    map = gui_img_create_from_fs(win_map, "map", "/resource/map/map_00.bin", 141, 198, 518, 282);
    gui_img_set_mode((gui_img_t *)map, IMG_BYPASS_MODE);
    gui_obj_show((gui_obj_t *)map, true);
    // Bind timer: map array
    gui_obj_create_timer((gui_obj_t *)map, 1000, true, map_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)map);

    // Create icon_dir (hg_image)
    icon_dir = gui_img_create_from_fs(win_map, "icon_dir", "/resource/arrow_direct.bin", 390, 106, 19, 35);
    gui_img_set_mode((gui_img_t *)icon_dir, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)icon_dir, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)icon_dir, false);
    // Bind timer: dir update
    gui_obj_create_timer((gui_obj_t *)icon_dir, 1000, true, icon_dir_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)icon_dir);

    // Create win_func (hg_window)
    win_func = gui_win_create((gui_obj_t *)view, "win_func", 150, 200, 500, 280);
    gui_obj_show((gui_obj_t *)win_func, false);


    // Create win_app_list (hg_window)
    win_app_list = gui_win_create(win_func, "win_app_list", 0, 0, 500, 280);
    gui_obj_show((gui_obj_t *)win_app_list, true);


    // Create app_list (hg_list)
    app_list = gui_list_create(win_app_list, "app_list", 0, 0, 500, 280, 280, 0, VERTICAL, app_list_note_design, NULL, false);
    gui_list_set_style(app_list, LIST_CLASSIC);
    gui_list_set_note_num(app_list, 2);
    gui_list_set_inertia(app_list, false);
    gui_obj_show((gui_obj_t *)app_list, true);

    // Create tab_hightlight (hg_image)
    tab_hightlight = gui_img_create_from_fs(win_app_list, "tab_hightlight", "/resource/func/tab_highlight.bin", 32, 0, 420, 94);
    gui_img_set_mode((gui_img_t *)tab_hightlight, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)tab_hightlight, 0xFF1BADDE);
    gui_obj_show((gui_obj_t *)tab_hightlight, true);
    // Bind timer: pos update
    gui_obj_create_timer((gui_obj_t *)tab_hightlight, 20, true, tab_hightlight_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)tab_hightlight);

    gui_obj_add_event_cb(GUI_BASE(win_app_list), (gui_event_cb_t)win_app_list_key_0_cb, GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)win_app_list);

    // Create win_app_weather (hg_window)
    win_app_weather = gui_win_create(win_func, "win_app_weather", 0, 0, 500, 280);
    gui_obj_show((gui_obj_t *)win_app_weather, false);


    // Create list_weather (hg_list)
    list_weather = gui_list_create(win_app_weather, "list_weather", 0, 0, 500, 280, 280, 0, VERTICAL, list_weather_note_design, NULL, false);
    gui_list_set_style(list_weather, LIST_CLASSIC);
    gui_list_set_note_num(list_weather, 2);
    gui_list_set_inertia(list_weather, false);
    gui_obj_show((gui_obj_t *)list_weather, true);

    gui_obj_add_event_cb(GUI_BASE(win_app_weather), (gui_event_cb_t)win_app_weather_key_0_cb, GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)win_app_weather);

    // Create win_app_developing (hg_window)
    win_app_developing = gui_win_create(win_func, "win_app_developing", 0, 0, 500, 280);
    gui_obj_show((gui_obj_t *)win_app_developing, false);


    // Create text_app_developing (hg_label)
    text_app_developing = gui_text_create(win_app_developing, "text_app_developing", 0, 0, 500, 280);
    gui_text_set((gui_text_t *)text_app_developing, "App is developing!", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 18, 36);
    gui_text_type_set((gui_text_t *)text_app_developing, "//HarmonyOS_Sans_Bold_size36_bits4_bitmap.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_app_developing, MID_CENTER);

    gui_obj_add_event_cb(GUI_BASE(win_app_developing), (gui_event_cb_t)win_app_developing_key_0_cb, GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)win_app_developing);

    // Create root_menu (hg_image)
    root_menu = gui_img_create_from_fs((gui_obj_t *)view, "root_menu", "/resource/menu/menu_001.bin", 225, 480, 350, 190);
    gui_img_set_mode((gui_img_t *)root_menu, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)root_menu, true);
    // Bind timer: set focus
    gui_obj_create_timer((gui_obj_t *)root_menu, 10, true, root_menu_timer_11_cb);
    gui_obj_add_event_cb(root_menu, (gui_event_cb_t)root_menu_key_cb, GUI_EVENT_KB_SHORT_PRESSED, NULL);
    gui_obj_focus_set((gui_obj_t *)root_menu);

    // Create win_dashboard (hg_window)
    win_dashboard = gui_win_create((gui_obj_t *)view, "win_dashboard", 0, 0, 800, 480);
    gui_obj_show((gui_obj_t *)win_dashboard, true);


    // Create win_line (hg_window)
    win_line = gui_win_create(win_dashboard, "win_line", 331, 53, 138, 100);
    gui_obj_show((gui_obj_t *)win_line, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)win_line, 30, true, win_line_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)win_line);


    // Create line_left (hg_image)
    line_left = gui_img_create_from_fs(win_line, "line_left", "/resource/LeftLineV1.bin", -306, 0, 375, 417);
    gui_img_set_mode((gui_img_t *)line_left, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)line_left, 0xFF00dce3);
    ((gui_img_t *)line_left)->need_clip = true;
    gui_obj_show((gui_obj_t *)line_left, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)line_left, 30, true, line_left_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)line_left);

    // Create line_right (hg_image)
    line_right = gui_img_create_from_fs(win_line, "line_right", "/resource/RightLineV1.bin", 69, 0, 375, 417);
    gui_img_set_mode((gui_img_t *)line_right, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)line_right, 0xFF00dce3);
    ((gui_img_t *)line_right)->need_clip = true;
    gui_obj_show((gui_obj_t *)line_right, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)line_right, 30, true, line_right_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)line_right);

    // Create win_dail (hg_window)
    win_dail = gui_win_create(win_dashboard, "win_dail", 0, 0, 800, 480);
    gui_obj_show((gui_obj_t *)win_dail, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)win_dail, 7, true, win_dail_timer_0_cb);


    // Create win_speed (hg_window)
    win_speed = gui_win_create(win_dail, "win_speed", 38, 64, 295, 294);
    gui_obj_show((gui_obj_t *)win_speed, true);


    // Create dail_speed (hg_image)
    dail_speed = gui_img_create_from_fs(win_speed, "dail_speed", "/resource/dail_speed.bin", 0, 0, 295, 294);
    gui_img_set_mode((gui_img_t *)dail_speed, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)dail_speed, true);

    // Create speed_arc (hg_image)
    speed_arc = gui_img_create_from_fs(win_speed, "speed_arc", "/resource/speed_arc/speed_arc_00.bin", 56, 55, 182, 182);
    gui_img_set_mode((gui_img_t *)speed_arc, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)speed_arc, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)speed_arc, 30, true, speed_arc_timer_1_cb);
    gui_obj_start_timer((gui_obj_t *)speed_arc);

    // Create dail_pointer_s (hg_image)
    dail_pointer_s = gui_img_create_from_fs(win_speed, "dail_pointer_s", "/resource/dail_pointer.bin", 144, 212, 4, 25);
    gui_img_set_mode((gui_img_t *)dail_pointer_s, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)dail_pointer_s, 0xFFFFFFFF);
    gui_img_set_quality((gui_img_t *)dail_pointer_s, true);
    gui_img_translate((gui_img_t *)dail_pointer_s, 2.0f, -65.0f);
    gui_img_set_focus((gui_img_t *)dail_pointer_s, 2.0f, -65.0f);
    gui_obj_show((gui_obj_t *)dail_pointer_s, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)dail_pointer_s, 30, true, dail_pointer_s_timer_1_cb);
    gui_obj_start_timer((gui_obj_t *)dail_pointer_s);

    // Create win_power (hg_window)
    win_power = gui_win_create(win_dail, "win_power", 470, 64, 295, 294);
    gui_obj_show((gui_obj_t *)win_power, true);


    // Create dail_power (hg_image)
    dail_power = gui_img_create_from_fs(win_power, "dail_power", "/resource/dail_power.bin", 0, 0, 295, 294);
    gui_img_set_mode((gui_img_t *)dail_power, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)dail_power, true);
    gui_obj_add_event_cb(dail_power, (gui_event_cb_t)dail_power_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create power_arc (hg_image)
    power_arc = gui_img_create_from_fs(win_power, "power_arc", "/resource/power_arc/power_arc_00.bin", 56, 55, 182, 182);
    gui_img_set_mode((gui_img_t *)power_arc, IMG_SRC_OVER_MODE);
    gui_obj_show((gui_obj_t *)power_arc, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)power_arc, 30, true, power_arc_timer_1_cb);
    gui_obj_start_timer((gui_obj_t *)power_arc);

    // Create dail_pointer_p (hg_image)
    dail_pointer_p = gui_img_create_from_fs(win_power, "dail_pointer_p", "/resource/dail_pointer.bin", 144, 212, 4, 25);
    gui_img_set_mode((gui_img_t *)dail_pointer_p, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)dail_pointer_p, 0xFFFFFFFF);
    gui_img_set_quality((gui_img_t *)dail_pointer_p, true);
    gui_img_translate((gui_img_t *)dail_pointer_p, 2.0f, -65.0f);
    gui_img_set_focus((gui_img_t *)dail_pointer_p, 2.0f, -65.0f);
    gui_obj_show((gui_obj_t *)dail_pointer_p, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)dail_pointer_p, 30, true, dail_pointer_p_timer_1_cb);
    gui_obj_start_timer((gui_obj_t *)dail_pointer_p);

    // Create rightbottom_gloom (hg_image)
    rightbottom_gloom = gui_img_create_from_fs(win_dail, "rightbottom_gloom", "/resource/RightBottomGloomV1.bin", 662, 353, 98, 127);
    gui_img_set_mode((gui_img_t *)rightbottom_gloom, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)rightbottom_gloom, 0xFF01b8f6);
    gui_obj_show((gui_obj_t *)rightbottom_gloom, true);

    // Create leftbottom_gloom (hg_image)
    leftbottom_gloom = gui_img_create_from_fs(win_dail, "leftbottom_gloom", "/resource/LeftBottomGloomV1.bin", 40, 353, 98, 127);
    gui_img_set_mode((gui_img_t *)leftbottom_gloom, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)leftbottom_gloom, 0xFF01b8f6);
    gui_obj_show((gui_obj_t *)leftbottom_gloom, true);

    // Create win_common (hg_window)
    win_common = gui_win_create((gui_obj_t *)view, "win_common", 0, 0, 800, 480);
    gui_obj_show((gui_obj_t *)win_common, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)win_common, 20, true, win_common_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)win_common);


    // Create turn_l_icon (hg_image)
    turn_l_icon = gui_img_create_from_fs(win_common, "turn_l_icon", "/resource/Turnleft_off.bin", 20, 64, 35, 19);
    gui_img_set_mode((gui_img_t *)turn_l_icon, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)turn_l_icon, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)turn_l_icon, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)turn_l_icon, 30, true, turn_l_icon_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)turn_l_icon);

    // Create turn_r_icon (hg_image)
    turn_r_icon = gui_img_create_from_fs(win_common, "turn_r_icon", "/resource/Turnright_off.bin", 745, 64, 35, 19);
    gui_img_set_mode((gui_img_t *)turn_r_icon, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)turn_r_icon, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)turn_r_icon, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)turn_r_icon, 30, true, turn_r_icon_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)turn_r_icon);

    // Create off_icon (hg_image)
    off_icon = gui_img_create_from_fs(win_common, "off_icon", "/resource/OffIcon_release.bin", 746, 23, 20, 20);
    gui_obj_show((gui_obj_t *)off_icon, true);
    gui_obj_add_event_cb(off_icon, (gui_event_cb_t)off_icon_clicked_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    // Create bt_icon (hg_image)
    bt_icon = gui_img_create_from_fs(win_common, "bt_icon", "/resource/bt.bin", 294, 52, 14, 20);
    gui_img_set_mode((gui_img_t *)bt_icon, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)bt_icon, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)bt_icon, true);

    // Create wifi_icon (hg_image)
    wifi_icon = gui_img_create_from_fs(win_common, "wifi_icon", "/resource/wifi.bin", 478, 52, 28, 20);
    gui_img_set_mode((gui_img_t *)wifi_icon, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)wifi_icon, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)wifi_icon, true);

    // Create text_loc_temp (hg_label)
    text_loc_temp = gui_text_create(win_common, "text_loc_temp", 334, 32, 132, 22);
    gui_text_set((gui_text_t *)text_loc_temp, "Suzhou 25°C", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 12, 22);
    gui_text_type_set((gui_text_t *)text_loc_temp, "//HarmonyOS_Sans_Bold_size22_bits4_bitmap.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_loc_temp, CENTER);

    // Create text_time (hg_label)
    text_time = gui_text_create(win_common, "text_time", 334, 55, 132, 22);
    gui_text_set((gui_text_t *)text_time, "10:10", GUI_FONT_SRC_BMP, gui_rgb(255, 255, 255), 5, 22);
    gui_text_type_set((gui_text_t *)text_time, "//HarmonyOS_Sans_Bold_size22_bits4_bitmap.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_time, CENTER);
    // Bind timer: time update
    gui_obj_create_timer((gui_obj_t *)text_time, 1000, true, text_time_timer_0_cb);

    // Create led_0 (hg_image)
    led_0 = gui_img_create_from_fs(win_common, "led_0", "/resource/LED0_off.bin", 85, 360, 27, 27);
    gui_img_set_mode((gui_img_t *)led_0, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)led_0, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)led_0, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)led_0, 30, true, led_0_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)led_0);

    // Create led_1 (hg_image)
    led_1 = gui_img_create_from_fs(win_common, "led_1", "/resource/LED1_off.bin", 65, 400, 27, 27);
    gui_img_set_mode((gui_img_t *)led_1, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)led_1, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)led_1, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)led_1, 30, true, led_1_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)led_1);

    // Create led_2 (hg_image)
    led_2 = gui_img_create_from_fs(win_common, "led_2", "/resource/LED2_off.bin", 45, 440, 27, 27);
    gui_img_set_mode((gui_img_t *)led_2, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)led_2, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)led_2, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)led_2, 30, true, led_2_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)led_2);

    // Create led_3 (hg_image)
    led_3 = gui_img_create_from_fs(win_common, "led_3", "/resource/LED3_off.bin", 690, 360, 27, 27);
    gui_img_set_mode((gui_img_t *)led_3, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)led_3, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)led_3, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)led_3, 30, true, led_3_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)led_3);

    // Create led_4 (hg_image)
    led_4 = gui_img_create_from_fs(win_common, "led_4", "/resource/LED4_off.bin", 710, 400, 27, 27);
    gui_img_set_mode((gui_img_t *)led_4, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)led_4, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)led_4, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)led_4, 30, true, led_4_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)led_4);

    // Create led_5 (hg_image)
    led_5 = gui_img_create_from_fs(win_common, "led_5", "/resource/LED5_off.bin", 730, 440, 26, 27);
    gui_img_set_mode((gui_img_t *)led_5, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)led_5, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)led_5, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)led_5, 30, true, led_5_timer_0_cb);
    gui_obj_start_timer((gui_obj_t *)led_5);

    // Create win_speed_text (hg_window)
    win_speed_text = gui_win_create(win_common, "win_speed_text", 38, 64, 294, 294);
    gui_obj_show((gui_obj_t *)win_speed_text, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)win_speed_text, 8, true, win_speed_text_timer_0_cb);


    // Create text_speed (hg_label)
    text_speed = gui_text_create(win_speed_text, "text_speed", 0, 106, 294, 40);
    gui_text_set((gui_text_t *)text_speed, "0", GUI_FONT_SRC_TTF, gui_rgb(255, 255, 255), 1, 40);
    gui_text_type_set((gui_text_t *)text_speed, "//HarmonyOS_Sans_Bold_vector.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_speed, CENTER);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)text_speed, 30, true, text_speed_timer_1_cb);
    gui_obj_start_timer((gui_obj_t *)text_speed);

    // Create text_speed_unit (hg_label)
    text_speed_unit = gui_text_create(win_speed_text, "text_speed_unit", 0, 141, 294, 26);
    gui_text_set((gui_text_t *)text_speed_unit, "km/h", GUI_FONT_SRC_TTF, gui_rgb(255, 255, 255), 4, 26);
    gui_text_type_set((gui_text_t *)text_speed_unit, "//HarmonyOS_Sans_Bold_vector.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_speed_unit, CENTER);

    // Create line (hg_image)
    line = gui_img_create_from_fs(win_speed_text, "line", "/resource/dividing_line.bin", 96, 168, 101, 2);
    gui_img_set_mode((gui_img_t *)line, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)line, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)line, true);

    // Create text_odo (hg_label)
    text_odo = gui_text_create(win_speed_text, "text_odo", 0, 177, 294, 16);
    gui_text_set((gui_text_t *)text_odo, "ODO 300km", GUI_FONT_SRC_TTF, gui_rgb(255, 255, 255), 9, 16);
    gui_text_type_set((gui_text_t *)text_odo, "//HarmonyOS_Sans_Bold_vector.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_odo, CENTER);

    // Create win_power_text (hg_window)
    win_power_text = gui_win_create(win_common, "win_power_text", 470, 64, 294, 294);
    gui_obj_show((gui_obj_t *)win_power_text, true);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)win_power_text, 8, true, win_power_text_timer_0_cb);


    // Create text_power (hg_label)
    text_power = gui_text_create(win_power_text, "text_power", 0, 106, 294, 40);
    gui_text_set((gui_text_t *)text_power, "0", GUI_FONT_SRC_TTF, gui_rgb(255, 255, 255), 1, 40);
    gui_text_type_set((gui_text_t *)text_power, "//HarmonyOS_Sans_Bold_vector.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_power, CENTER);
    // Bind timer: entrance
    gui_obj_create_timer((gui_obj_t *)text_power, 30, true, text_power_timer_1_cb);
    gui_obj_start_timer((gui_obj_t *)text_power);

    // Create text_power_unit (hg_label)
    text_power_unit = gui_text_create(win_power_text, "text_power_unit", 0, 141, 294, 26);
    gui_text_set((gui_text_t *)text_power_unit, "kW", GUI_FONT_SRC_TTF, gui_rgb(255, 255, 255), 2, 26);
    gui_text_type_set((gui_text_t *)text_power_unit, "//HarmonyOS_Sans_Bold_vector.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_power_unit, CENTER);

    // Create line_copy_1768894235876_3 (hg_image)
    line_copy_1768894235876_3 = gui_img_create_from_fs(win_power_text, "line_copy_1768894235876_3", "/resource/dividing_line.bin", 96, 168, 101, 2);
    gui_img_set_mode((gui_img_t *)line_copy_1768894235876_3, IMG_2D_SW_FIX_A8_FG);
    gui_img_a8_recolor((gui_img_t *)line_copy_1768894235876_3, 0xFFFFFFFF);
    gui_obj_show((gui_obj_t *)line_copy_1768894235876_3, true);

    // Create text_battery (hg_label)
    text_battery = gui_text_create(win_power_text, "text_battery", 0, 177, 294, 15);
    gui_text_set((gui_text_t *)text_battery, "BATT 65%", GUI_FONT_SRC_TTF, gui_rgb(255, 255, 255), 8, 16);
    gui_text_type_set((gui_text_t *)text_battery, "//HarmonyOS_Sans_Bold_vector.bin", FONT_SRC_FILESYS);
    gui_text_mode_set((gui_text_t *)text_battery, CENTER);
}
GUI_VIEW_INSTANCE("dashboard_view", false, dashboard_view_switch_in, dashboard_view_switch_out);
