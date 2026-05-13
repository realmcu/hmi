#include "DashboardMain_callbacks.h"
#include "../ui/DashboardMain_ui.h"
#include "../user/DashboardMain_user.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Timer animation counters
uint16_t gloom_timer_cnt = 0;
uint16_t shadow_timer_cnt = 0;
uint16_t win_map_timer_cnt = 0;
uint16_t icon_dir_timer_cnt = 0;
uint16_t map_timer_cnt = 0;
uint16_t root_menu_timer_cnt = 0;
uint16_t win_line_timer_cnt = 0;
uint16_t line_left_timer_cnt = 0;
uint16_t line_right_timer_cnt = 0;
uint16_t win_dail_timer_cnt = 0;
uint16_t speed_arc_timer_cnt = 0;
uint16_t dail_pointer_s_timer_cnt = 0;
uint16_t power_arc_timer_cnt = 0;
uint16_t dail_pointer_p_timer_cnt = 0;
uint16_t win_common_timer_cnt = 0;
uint16_t turn_l_icon_timer_cnt = 0;
uint16_t turn_r_icon_timer_cnt = 0;
uint16_t text_time_timer_cnt = 0;
uint16_t led_0_timer_cnt = 0;
uint16_t led_1_timer_cnt = 0;
uint16_t led_2_timer_cnt = 0;
uint16_t led_3_timer_cnt = 0;
uint16_t led_4_timer_cnt = 0;
uint16_t led_5_timer_cnt = 0;
uint16_t win_speed_text_timer_cnt = 0;
uint16_t text_speed_timer_cnt = 0;
uint16_t win_power_text_timer_cnt = 0;
uint16_t text_power_timer_cnt = 0;
uint16_t win_func_timer_cnt = 0;
uint16_t win_app_list_timer_cnt = 0;
uint16_t app_list_timer_cnt = 0;
uint16_t tab_hightlight_timer_cnt = 0;
uint16_t win_app_weather_timer_cnt = 0;
uint16_t list_weather_timer_cnt = 0;
uint16_t win_app_developing_timer_cnt = 0;

// Event callback function implementations

void start_engine_pressed_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    shadow_timer_cnt = 0; // Reset counter
    gui_obj_create_timer(GUI_BASE(shadow), 20, true, shadow_timer_0_cb);
    gui_obj_start_timer(GUI_BASE(shadow));
}

void start_engine_released_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    shadow_timer_cnt = 0; // Reset counter
    gui_obj_create_timer(GUI_BASE(shadow), 20, true, shadow_timer_1_cb);
    gui_obj_start_timer(GUI_BASE(shadow));
    gloom_timer_cnt = 0; // Reset counter
    gui_obj_create_timer(GUI_BASE(gloom), 20, true, gloom_timer_0_cb);
    gui_obj_start_timer(GUI_BASE(gloom));
}

void start_engine_key_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Power") == 0)
    {
        gloom_timer_cnt = 0; // Reset counter
        gui_obj_create_timer(GUI_BASE(gloom), 20, true, gloom_timer_0_cb);
        gui_obj_start_timer(GUI_BASE(gloom));
    }
}

void root_menu_key_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Home") == 0)
    {
        root_menu_msg_enter_cb(obj, e);
    }
    else if (strcmp(e->indev_name, "Back") == 0)
    {
        root_menu_msg_hide_cb(obj, e);
    }
    else if (strcmp(e->indev_name, "Menu") == 0)
    {
        root_menu_msg_show_cb(obj, e);
    }
    else if (strcmp(e->indev_name, "Power") == 0)
    {
        gui_view_switch_direct(gui_view_get_current(), "dashboardMainView", SWITCH_OUT_ANIMATION_FADE, SWITCH_IN_ANIMATION_FADE);
    }
}

void dail_power_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // TODO: Implement event handling logic
}

void off_icon_clicked_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    gui_view_switch_direct(gui_view_get_current(), "dashboardMainView", SWITCH_OUT_NONE_ANIMATION, SWITCH_IN_NONE_ANIMATION);
}

void win_app_list_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Home") == 0)
    {
        win_app_list_msg_enter_cb(obj, e);
    }
    else if (strcmp(e->indev_name, "Back") == 0)
    {
        win_app_list_msg_exit_cb(obj, e);
    }
    else if (strcmp(e->indev_name, "Menu") == 0)
    {
        win_app_list_msg_next_cb(obj, e);
    }
}

void win_app_weather_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Menu") == 0)
    {
        win_app_weather_msg_next_cb(obj, e);
    }
    else if (strcmp(e->indev_name, "Back") == 0)
    {
        win_app_weather_msg_exit_cb(obj, e);
    }
}

void win_app_developing_key_0_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    // Check key name
    if (strcmp(e->indev_name, "Back") == 0)
    {
        win_app_developing_msg_exit_cb(obj, e);
    }
}

// Preset timer callback functions

/**
 * exit
 * Component: gloom
 * Mode: Preset actions (multi-segment animation)
 * Segments: 3
 */
void gloom_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 36;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 30;
    const uint16_t seg1_start = 30;
    const uint16_t seg1_end = 35;
    const uint16_t seg2_start = 35;
    const uint16_t seg2_end = 36;
    
    gloom_timer_cnt++;
    
    // Segment 1: 600ms, 1 action(s)
    if (gloom_timer_cnt > seg0_start && gloom_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = gloom_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust opacity: 0 -> 255
            const uint8_t opacity_origin = 0;
            const uint8_t opacity_target = 255;
            int16_t opacity_cur = opacity_origin + (opacity_target - opacity_origin) * seg_cnt / seg_cnt_max;
            gui_img_set_opacity((gui_img_t *)target, opacity_cur);
            
    }
    // Segment 2: Wait 100ms
    else if (gloom_timer_cnt > seg1_start && gloom_timer_cnt <= seg1_end) {
        // No action, just wait
    }
    // Segment 3: 20ms, 1 action(s)
    else if (gloom_timer_cnt > seg2_start && gloom_timer_cnt <= seg2_end) {
            // Switch view: dashboard_view
            gui_view_switch_direct(gui_view_get_current(), "dashboard_view", SWITCH_OUT_NONE_ANIMATION, SWITCH_IN_NONE_ANIMATION);
            
    }
    
    if (gloom_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        gloom_timer_cnt = 0; // Reset counter
    }
}


/**
 * press
 * Component: shadow
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void shadow_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    shadow_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (shadow_timer_cnt > seg0_start && shadow_timer_cnt <= seg0_end) {
            // Change image: /resource/Start_Engine_Press.bin
            gui_img_set_src((gui_img_t *)target, (const uint8_t *)"/resource/Start_Engine_Press.bin", IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (shadow_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        shadow_timer_cnt = 0; // Reset counter
    }
}


/**
 * release
 * Component: shadow
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void shadow_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    shadow_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (shadow_timer_cnt > seg0_start && shadow_timer_cnt <= seg0_end) {
            // Change image: /resource/Start_Engine_Release.bin
            gui_img_set_src((gui_img_t *)target, (const uint8_t *)"/resource/Start_Engine_Release.bin", IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (shadow_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        shadow_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: win_map
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_map_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_map_timer_cnt++;
    
    // Segment 1: 6000ms, 1 action(s)
    if (win_map_timer_cnt > seg0_start && win_map_timer_cnt <= seg0_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
    }
    
    if (win_map_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_map_timer_cnt = 0; // Reset counter
    }
}


/**
 * show map
 * Component: win_map
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_map_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_map_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_map_timer_cnt > seg0_start && win_map_timer_cnt <= seg0_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
    }
    
    if (win_map_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_map_timer_cnt = 0; // Reset counter
    }
}


/**
 * hide map
 * Component: win_map
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_map_timer_2_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_map_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_map_timer_cnt > seg0_start && win_map_timer_cnt <= seg0_end) {
            // Set visibility: hide
            gui_obj_show(target, false);
            
    }
    
    if (win_map_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_map_timer_cnt = 0; // Reset counter
    }
}


/**
 * dir update
 * Component: icon_dir
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void icon_dir_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 13;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 13;
    
    icon_dir_timer_cnt++;
    
    // Segment 1: 13000ms, 1 action(s)
    if (icon_dir_timer_cnt > seg0_start && icon_dir_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = icon_dir_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 13 images
            const void *img_data_array[13] = {
                "/resource/arrow_direct.bin",
                "/resource/arrow_direct.bin",
                "/resource/arrow_direct.bin",
                "/resource/arrow_direct.bin",
                "/resource/Arrow_right.bin",
                "/resource/Arrow_right.bin",
                "/resource/arrow_direct.bin",
                "/resource/arrow_direct.bin",
                "/resource/Arrow_right.bin",
                "/resource/Arrow_left.bin",
                "/resource/Arrow_right.bin",
                "/resource/Arrow_left.bin",
                "/resource/arrow_direct.bin"
            };
            uint16_t index = (13 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (icon_dir_timer_cnt >= total_cnt_max) {
        icon_dir_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * map array
 * Component: map
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void map_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 13;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 13;
    
    map_timer_cnt++;
    
    // Segment 1: 13000ms, 1 action(s)
    if (map_timer_cnt > seg0_start && map_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = map_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 13 images
            const void *img_data_array[13] = {
                "/resource/map/map_00.bin",
                "/resource/map/map_01.bin",
                "/resource/map/map_02.bin",
                "/resource/map/map_03.bin",
                "/resource/map/map_04.bin",
                "/resource/map/map_05.bin",
                "/resource/map/map_06.bin",
                "/resource/map/map_07.bin",
                "/resource/map/map_08.bin",
                "/resource/map/map_09.bin",
                "/resource/map/map_10.bin",
                "/resource/map/map_11.bin",
                "/resource/map/map_12.bin"
            };
            uint16_t index = (13 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (map_timer_cnt >= total_cnt_max) {
        map_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * entrance
 * Component: map
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void map_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    map_timer_cnt++;
    
    // Segment 1: 6000ms, 1 action(s)
    if (map_timer_cnt > seg0_start && map_timer_cnt <= seg0_end) {
            // Timer control
            // Start timer animation: map array
            map_timer_cnt = 0; // Reset counter
            gui_obj_create_timer(target, 1000, true, map_timer_0_cb);
            gui_obj_start_timer(target);
            return; // Return immediately after timer control
            
    }
    
    if (map_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        map_timer_cnt = 0; // Reset counter
    }
}


/**
 * menu array
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 120;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 120;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 2400ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 60 images
            const void *img_data_array[60] = {
                "/resource/menu/menu_001.bin",
                "/resource/menu/menu_002.bin",
                "/resource/menu/menu_003.bin",
                "/resource/menu/menu_004.bin",
                "/resource/menu/menu_005.bin",
                "/resource/menu/menu_006.bin",
                "/resource/menu/menu_007.bin",
                "/resource/menu/menu_008.bin",
                "/resource/menu/menu_009.bin",
                "/resource/menu/menu_010.bin",
                "/resource/menu/menu_011.bin",
                "/resource/menu/menu_012.bin",
                "/resource/menu/menu_013.bin",
                "/resource/menu/menu_014.bin",
                "/resource/menu/menu_015.bin",
                "/resource/menu/menu_016.bin",
                "/resource/menu/menu_017.bin",
                "/resource/menu/menu_018.bin",
                "/resource/menu/menu_019.bin",
                "/resource/menu/menu_020.bin",
                "/resource/menu/menu_021.bin",
                "/resource/menu/menu_022.bin",
                "/resource/menu/menu_023.bin",
                "/resource/menu/menu_024.bin",
                "/resource/menu/menu_025.bin",
                "/resource/menu/menu_026.bin",
                "/resource/menu/menu_027.bin",
                "/resource/menu/menu_028.bin",
                "/resource/menu/menu_029.bin",
                "/resource/menu/menu_030.bin",
                "/resource/menu/menu_031.bin",
                "/resource/menu/menu_032.bin",
                "/resource/menu/menu_033.bin",
                "/resource/menu/menu_034.bin",
                "/resource/menu/menu_035.bin",
                "/resource/menu/menu_036.bin",
                "/resource/menu/menu_037.bin",
                "/resource/menu/menu_038.bin",
                "/resource/menu/menu_039.bin",
                "/resource/menu/menu_040.bin",
                "/resource/menu/menu_041.bin",
                "/resource/menu/menu_042.bin",
                "/resource/menu/menu_043.bin",
                "/resource/menu/menu_044.bin",
                "/resource/menu/menu_045.bin",
                "/resource/menu/menu_046.bin",
                "/resource/menu/menu_047.bin",
                "/resource/menu/menu_048.bin",
                "/resource/menu/menu_049.bin",
                "/resource/menu/menu_050.bin",
                "/resource/menu/menu_051.bin",
                "/resource/menu/menu_052.bin",
                "/resource/menu/menu_053.bin",
                "/resource/menu/menu_054.bin",
                "/resource/menu/menu_055.bin",
                "/resource/menu/menu_056.bin",
                "/resource/menu/menu_057.bin",
                "/resource/menu/menu_058.bin",
                "/resource/menu/menu_059.bin",
                "/resource/menu/menu_060.bin"
            };
            uint16_t index = (60 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        root_menu_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * move up
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 15;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 15;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 150ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (225, 480) -> (225, 293)
            const int16_t x_origin = 225;
            const int16_t y_origin = 480;
            const int16_t x_target = 225;
            const int16_t y_target = 293;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * move down
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_2_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 15;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 15;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 150ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (225, 293) -> (225, 480)
            const int16_t x_origin = 225;
            const int16_t y_origin = 293;
            const int16_t x_target = 225;
            const int16_t y_target = 480;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 1 to 2
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_3_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 15;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 15;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 300ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 15 images
            const void *img_data_array[15] = {
                "/resource/menu/menu_001.bin",
                "/resource/menu/menu_002.bin",
                "/resource/menu/menu_003.bin",
                "/resource/menu/menu_004.bin",
                "/resource/menu/menu_005.bin",
                "/resource/menu/menu_006.bin",
                "/resource/menu/menu_007.bin",
                "/resource/menu/menu_008.bin",
                "/resource/menu/menu_009.bin",
                "/resource/menu/menu_010.bin",
                "/resource/menu/menu_011.bin",
                "/resource/menu/menu_012.bin",
                "/resource/menu/menu_013.bin",
                "/resource/menu/menu_014.bin",
                "/resource/menu/menu_015.bin"
            };
            uint16_t index = (15 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 2 to 3
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_4_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 15;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 15;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 300ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 15 images
            const void *img_data_array[15] = {
                "/resource/menu/menu_016.bin",
                "/resource/menu/menu_017.bin",
                "/resource/menu/menu_018.bin",
                "/resource/menu/menu_019.bin",
                "/resource/menu/menu_020.bin",
                "/resource/menu/menu_021.bin",
                "/resource/menu/menu_022.bin",
                "/resource/menu/menu_023.bin",
                "/resource/menu/menu_024.bin",
                "/resource/menu/menu_025.bin",
                "/resource/menu/menu_026.bin",
                "/resource/menu/menu_027.bin",
                "/resource/menu/menu_028.bin",
                "/resource/menu/menu_029.bin",
                "/resource/menu/menu_030.bin"
            };
            uint16_t index = (15 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 3 to 4
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_5_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 15;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 15;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 300ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 15 images
            const void *img_data_array[15] = {
                "/resource/menu/menu_031.bin",
                "/resource/menu/menu_032.bin",
                "/resource/menu/menu_033.bin",
                "/resource/menu/menu_034.bin",
                "/resource/menu/menu_035.bin",
                "/resource/menu/menu_036.bin",
                "/resource/menu/menu_037.bin",
                "/resource/menu/menu_038.bin",
                "/resource/menu/menu_039.bin",
                "/resource/menu/menu_040.bin",
                "/resource/menu/menu_041.bin",
                "/resource/menu/menu_042.bin",
                "/resource/menu/menu_043.bin",
                "/resource/menu/menu_044.bin",
                "/resource/menu/menu_045.bin"
            };
            uint16_t index = (15 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 4 to 1
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_6_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 15;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 15;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 299ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 15 images
            const void *img_data_array[15] = {
                "/resource/menu/menu_046.bin",
                "/resource/menu/menu_047.bin",
                "/resource/menu/menu_048.bin",
                "/resource/menu/menu_049.bin",
                "/resource/menu/menu_050.bin",
                "/resource/menu/menu_051.bin",
                "/resource/menu/menu_052.bin",
                "/resource/menu/menu_053.bin",
                "/resource/menu/menu_054.bin",
                "/resource/menu/menu_055.bin",
                "/resource/menu/menu_056.bin",
                "/resource/menu/menu_057.bin",
                "/resource/menu/menu_058.bin",
                "/resource/menu/menu_059.bin",
                "/resource/menu/menu_060.bin"
            };
            uint16_t index = (15 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 1 to 4
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_7_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 14;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 14;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 280ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 14 images
            const void *img_data_array[14] = {
                "/resource/menu/menu_060.bin",
                "/resource/menu/menu_059.bin",
                "/resource/menu/menu_058.bin",
                "/resource/menu/menu_057.bin",
                "/resource/menu/menu_056.bin",
                "/resource/menu/menu_055.bin",
                "/resource/menu/menu_054.bin",
                "/resource/menu/menu_053.bin",
                "/resource/menu/menu_052.bin",
                "/resource/menu/menu_051.bin",
                "/resource/menu/menu_050.bin",
                "/resource/menu/menu_049.bin",
                "/resource/menu/menu_048.bin",
                "/resource/menu/menu_047.bin"
            };
            uint16_t index = (14 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 4 to 3
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_8_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 14;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 14;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 280ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 14 images
            const void *img_data_array[14] = {
                "/resource/menu/menu_045.bin",
                "/resource/menu/menu_044.bin",
                "/resource/menu/menu_043.bin",
                "/resource/menu/menu_042.bin",
                "/resource/menu/menu_041.bin",
                "/resource/menu/menu_040.bin",
                "/resource/menu/menu_039.bin",
                "/resource/menu/menu_038.bin",
                "/resource/menu/menu_037.bin",
                "/resource/menu/menu_036.bin",
                "/resource/menu/menu_035.bin",
                "/resource/menu/menu_034.bin",
                "/resource/menu/menu_033.bin",
                "/resource/menu/menu_032.bin"
            };
            uint16_t index = (14 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 3 to 2
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_9_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 14;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 14;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 280ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 14 images
            const void *img_data_array[14] = {
                "/resource/menu/menu_030.bin",
                "/resource/menu/menu_029.bin",
                "/resource/menu/menu_028.bin",
                "/resource/menu/menu_027.bin",
                "/resource/menu/menu_026.bin",
                "/resource/menu/menu_025.bin",
                "/resource/menu/menu_024.bin",
                "/resource/menu/menu_023.bin",
                "/resource/menu/menu_022.bin",
                "/resource/menu/menu_021.bin",
                "/resource/menu/menu_020.bin",
                "/resource/menu/menu_019.bin",
                "/resource/menu/menu_018.bin",
                "/resource/menu/menu_017.bin"
            };
            uint16_t index = (14 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * 2 to 1
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_10_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 14;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 14;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 280ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = root_menu_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 14 images
            const void *img_data_array[14] = {
                "/resource/menu/menu_015.bin",
                "/resource/menu/menu_014.bin",
                "/resource/menu/menu_013.bin",
                "/resource/menu/menu_012.bin",
                "/resource/menu/menu_011.bin",
                "/resource/menu/menu_010.bin",
                "/resource/menu/menu_009.bin",
                "/resource/menu/menu_008.bin",
                "/resource/menu/menu_007.bin",
                "/resource/menu/menu_006.bin",
                "/resource/menu/menu_005.bin",
                "/resource/menu/menu_004.bin",
                "/resource/menu/menu_003.bin",
                "/resource/menu/menu_002.bin"
            };
            uint16_t index = (14 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * set focus
 * Component: root_menu
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void root_menu_timer_11_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    root_menu_timer_cnt++;
    
    // Segment 1: 10ms, 1 action(s)
    if (root_menu_timer_cnt > seg0_start && root_menu_timer_cnt <= seg0_end) {
            // Set focus
            gui_obj_focus_set(target);
            
    }
    
    if (root_menu_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        root_menu_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: win_line
 * Mode: Preset actions (multi-segment animation)
 * Segments: 2
 */
void win_line_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 62;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 30;
    const uint16_t seg1_start = 30;
    const uint16_t seg1_end = 62;
    
    win_line_timer_cnt++;
    
    // Segment 1: 900ms, 2 action(s)
    if (win_line_timer_cnt > seg0_start && win_line_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = win_line_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust size: (138, 100) -> (750, 100)
            const int16_t w_origin = 138;
            const int16_t h_origin = 100;
            const int16_t w_target = 750;
            const int16_t h_target = 100;
            int16_t w_cur = w_origin + (w_target - w_origin) * seg_cnt / seg_cnt_max;
            int16_t h_cur = h_origin + (h_target - h_origin) * seg_cnt / seg_cnt_max;
            target->w = w_cur;
            target->h = h_cur;
            
            // Adjust position: (331, 53) -> (25, 53)
            const int16_t x_origin = 331;
            const int16_t y_origin = 53;
            const int16_t x_target = 25;
            const int16_t y_target = 53;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    // Segment 2: 960ms, 1 action(s)
    else if (win_line_timer_cnt > seg1_start && win_line_timer_cnt <= seg1_end) {
        uint16_t seg_cnt = win_line_timer_cnt - seg1_start;
        const uint16_t seg_cnt_max = seg1_end - seg1_start;
        
            // Adjust size: (750, 100) -> (750, 417)
            const int16_t w_origin = 750;
            const int16_t h_origin = 100;
            const int16_t w_target = 750;
            const int16_t h_target = 417;
            int16_t w_cur = w_origin + (w_target - w_origin) * seg_cnt / seg_cnt_max;
            int16_t h_cur = h_origin + (h_target - h_origin) * seg_cnt / seg_cnt_max;
            target->w = w_cur;
            target->h = h_cur;
            
    }
    
    if (win_line_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_line_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: line_left
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void line_left_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 30;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 30;
    
    line_left_timer_cnt++;
    
    // Segment 1: 900ms, 1 action(s)
    if (line_left_timer_cnt > seg0_start && line_left_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = line_left_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (-306, 0) -> (0, 0)
            const int16_t x_origin = -306;
            const int16_t y_origin = 0;
            const int16_t x_target = 0;
            const int16_t y_target = 0;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (line_left_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        line_left_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: line_right
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void line_right_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 30;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 30;
    
    line_right_timer_cnt++;
    
    // Segment 1: 900ms, 1 action(s)
    if (line_right_timer_cnt > seg0_start && line_right_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = line_right_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (69, 0) -> (375, 0)
            const int16_t x_origin = 69;
            const int16_t y_origin = 0;
            const int16_t x_target = 375;
            const int16_t y_target = 0;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (line_right_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        line_right_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: win_dail
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_dail_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 143;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 143;
    
    win_dail_timer_cnt++;
    
    // Segment 1: 1000ms, 1 action(s)
    if (win_dail_timer_cnt > seg0_start && win_dail_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = win_dail_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust opacity: 1 -> 255
            const uint8_t opacity_origin = 1;
            const uint8_t opacity_target = 255;
            int16_t opacity_cur = opacity_origin + (opacity_target - opacity_origin) * seg_cnt / seg_cnt_max;
            target->opacity_value = opacity_cur;
            
    }
    
    if (win_dail_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_dail_timer_cnt = 0; // Reset counter
    }
}


/**
 * show
 * Component: win_dail
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_dail_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_dail_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_dail_timer_cnt > seg0_start && win_dail_timer_cnt <= seg0_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
    }
    
    if (win_dail_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_dail_timer_cnt = 0; // Reset counter
    }
}


/**
 * hide
 * Component: win_dail
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_dail_timer_2_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_dail_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_dail_timer_cnt > seg0_start && win_dail_timer_cnt <= seg0_end) {
            // Set visibility: hide
            gui_obj_show(target, false);
            
    }
    
    if (win_dail_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_dail_timer_cnt = 0; // Reset counter
    }
}


/**
 * arc_animate
 * Component: speed_arc
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void speed_arc_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 16;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 16;
    
    speed_arc_timer_cnt++;
    
    // Segment 1: 1600ms, 1 action(s)
    if (speed_arc_timer_cnt > seg0_start && speed_arc_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = speed_arc_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 16 images
            const void *img_data_array[16] = {
                "/resource/speed_arc/speed_arc_01.bin",
                "/resource/speed_arc/speed_arc_02.bin",
                "/resource/speed_arc/speed_arc_03.bin",
                "/resource/speed_arc/speed_arc_04.bin",
                "/resource/speed_arc/speed_arc_05.bin",
                "/resource/speed_arc/speed_arc_06.bin",
                "/resource/speed_arc/speed_arc_07.bin",
                "/resource/speed_arc/speed_arc_08.bin",
                "/resource/speed_arc/speed_arc_09.bin",
                "/resource/speed_arc/speed_arc_10.bin",
                "/resource/speed_arc/speed_arc_11.bin",
                "/resource/speed_arc/speed_arc_12.bin",
                "/resource/speed_arc/speed_arc_13.bin",
                "/resource/speed_arc/speed_arc_14.bin",
                "/resource/speed_arc/speed_arc_15.bin",
                "/resource/speed_arc/speed_arc_00.bin"
            };
            uint16_t index = (16 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (speed_arc_timer_cnt >= total_cnt_max) {
        speed_arc_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * entrance
 * Component: speed_arc
 * Mode: Preset actions (multi-segment animation)
 * Segments: 5
 */
void speed_arc_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 164;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 97;
    const uint16_t seg1_start = 97;
    const uint16_t seg1_end = 113;
    const uint16_t seg2_start = 113;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 163;
    const uint16_t seg4_start = 163;
    const uint16_t seg4_end = 164;
    
    speed_arc_timer_cnt++;
    
    // Segment 1: Wait 2900ms
    if (speed_arc_timer_cnt > seg0_start && speed_arc_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 480ms, 1 action(s)
    else if (speed_arc_timer_cnt > seg1_start && speed_arc_timer_cnt <= seg1_end) {
        uint16_t seg_cnt = speed_arc_timer_cnt - seg1_start;
        const uint16_t seg_cnt_max = seg1_end - seg1_start;
        
            // Image sequence animation: 16 images
            const void *img_data_array[16] = {
                "/resource/speed_arc/speed_arc_01.bin",
                "/resource/speed_arc/speed_arc_02.bin",
                "/resource/speed_arc/speed_arc_03.bin",
                "/resource/speed_arc/speed_arc_04.bin",
                "/resource/speed_arc/speed_arc_05.bin",
                "/resource/speed_arc/speed_arc_06.bin",
                "/resource/speed_arc/speed_arc_07.bin",
                "/resource/speed_arc/speed_arc_08.bin",
                "/resource/speed_arc/speed_arc_09.bin",
                "/resource/speed_arc/speed_arc_10.bin",
                "/resource/speed_arc/speed_arc_11.bin",
                "/resource/speed_arc/speed_arc_12.bin",
                "/resource/speed_arc/speed_arc_13.bin",
                "/resource/speed_arc/speed_arc_14.bin",
                "/resource/speed_arc/speed_arc_15.bin",
                "/resource/speed_arc/speed_arc_16.bin"
            };
            uint16_t index = (16 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    // Segment 3: 480ms, 1 action(s)
    else if (speed_arc_timer_cnt > seg2_start && speed_arc_timer_cnt <= seg2_end) {
        uint16_t seg_cnt = speed_arc_timer_cnt - seg2_start;
        const uint16_t seg_cnt_max = seg2_end - seg2_start;
        
            // Image sequence animation: 16 images
            const void *img_data_array[16] = {
                "/resource/speed_arc/speed_arc_15.bin",
                "/resource/speed_arc/speed_arc_14.bin",
                "/resource/speed_arc/speed_arc_13.bin",
                "/resource/speed_arc/speed_arc_12.bin",
                "/resource/speed_arc/speed_arc_11.bin",
                "/resource/speed_arc/speed_arc_10.bin",
                "/resource/speed_arc/speed_arc_09.bin",
                "/resource/speed_arc/speed_arc_08.bin",
                "/resource/speed_arc/speed_arc_07.bin",
                "/resource/speed_arc/speed_arc_06.bin",
                "/resource/speed_arc/speed_arc_05.bin",
                "/resource/speed_arc/speed_arc_04.bin",
                "/resource/speed_arc/speed_arc_03.bin",
                "/resource/speed_arc/speed_arc_02.bin",
                "/resource/speed_arc/speed_arc_01.bin",
                "/resource/speed_arc/speed_arc_00.bin"
            };
            uint16_t index = (16 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    // Segment 4: Wait 1000ms
    else if (speed_arc_timer_cnt > seg3_start && speed_arc_timer_cnt <= seg3_end) {
        // No action, just wait
    }
    // Segment 5: 30ms, 1 action(s)
    else if (speed_arc_timer_cnt > seg4_start && speed_arc_timer_cnt <= seg4_end) {
            // Timer control
            // Start timer animation: arc_animate
            speed_arc_timer_cnt = 0; // Reset counter
            gui_obj_create_timer(target, 100, true, speed_arc_timer_0_cb);
            gui_obj_start_timer(target);
            return; // Return immediately after timer control
            
    }
    
    if (speed_arc_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        speed_arc_timer_cnt = 0; // Reset counter
    }
}


/**
 * rotate
 * Component: dail_pointer_s
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void dail_pointer_s_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 16;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 16;
    
    dail_pointer_s_timer_cnt++;
    
    // Segment 1: 1600ms, 1 action(s)
    if (dail_pointer_s_timer_cnt > seg0_start && dail_pointer_s_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = dail_pointer_s_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust rotation: 0° -> 360°
            const float angle_origin = 0;
            const float angle_target = 360;
            float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
            gui_img_rotation((gui_img_t *)target, angle_cur);
            
    }
    
    if (dail_pointer_s_timer_cnt >= total_cnt_max) {
        dail_pointer_s_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * entrance
 * Component: dail_pointer_s
 * Mode: Preset actions (multi-segment animation)
 * Segments: 5
 */
void dail_pointer_s_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 164;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 97;
    const uint16_t seg1_start = 97;
    const uint16_t seg1_end = 113;
    const uint16_t seg2_start = 113;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 163;
    const uint16_t seg4_start = 163;
    const uint16_t seg4_end = 164;
    
    dail_pointer_s_timer_cnt++;
    
    // Segment 1: Wait 2900ms
    if (dail_pointer_s_timer_cnt > seg0_start && dail_pointer_s_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 480ms, 1 action(s)
    else if (dail_pointer_s_timer_cnt > seg1_start && dail_pointer_s_timer_cnt <= seg1_end) {
        uint16_t seg_cnt = dail_pointer_s_timer_cnt - seg1_start;
        const uint16_t seg_cnt_max = seg1_end - seg1_start;
        
            // Adjust rotation: 0° -> 360°
            const float angle_origin = 0;
            const float angle_target = 360;
            float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
            gui_img_rotation((gui_img_t *)target, angle_cur);
            
    }
    // Segment 3: 480ms, 1 action(s)
    else if (dail_pointer_s_timer_cnt > seg2_start && dail_pointer_s_timer_cnt <= seg2_end) {
        uint16_t seg_cnt = dail_pointer_s_timer_cnt - seg2_start;
        const uint16_t seg_cnt_max = seg2_end - seg2_start;
        
            // Adjust rotation: 720° -> 360°
            const float angle_origin = 720;
            const float angle_target = 360;
            float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
            gui_img_rotation((gui_img_t *)target, angle_cur);
            
    }
    // Segment 4: Wait 1000ms
    else if (dail_pointer_s_timer_cnt > seg3_start && dail_pointer_s_timer_cnt <= seg3_end) {
        // No action, just wait
    }
    // Segment 5: 30ms, 1 action(s)
    else if (dail_pointer_s_timer_cnt > seg4_start && dail_pointer_s_timer_cnt <= seg4_end) {
            // Timer control
            // Start timer animation: rotate
            dail_pointer_s_timer_cnt = 0; // Reset counter
            gui_obj_create_timer(target, 100, true, dail_pointer_s_timer_0_cb);
            gui_obj_start_timer(target);
            return; // Return immediately after timer control
            
    }
    
    if (dail_pointer_s_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        dail_pointer_s_timer_cnt = 0; // Reset counter
    }
}


/**
 * arc_animate
 * Component: power_arc
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void power_arc_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 16;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 16;
    
    power_arc_timer_cnt++;
    
    // Segment 1: 1600ms, 1 action(s)
    if (power_arc_timer_cnt > seg0_start && power_arc_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = power_arc_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Image sequence animation: 16 images
            const void *img_data_array[16] = {
                "/resource/power_arc/power_arc_01.bin",
                "/resource/power_arc/power_arc_02.bin",
                "/resource/power_arc/power_arc_03.bin",
                "/resource/power_arc/power_arc_04.bin",
                "/resource/power_arc/power_arc_05.bin",
                "/resource/power_arc/power_arc_06.bin",
                "/resource/power_arc/power_arc_07.bin",
                "/resource/power_arc/power_arc_08.bin",
                "/resource/power_arc/power_arc_09.bin",
                "/resource/power_arc/power_arc_10.bin",
                "/resource/power_arc/power_arc_11.bin",
                "/resource/power_arc/power_arc_12.bin",
                "/resource/power_arc/power_arc_13.bin",
                "/resource/power_arc/power_arc_14.bin",
                "/resource/power_arc/power_arc_15.bin",
                "/resource/power_arc/power_arc_00.bin"
            };
            uint16_t index = (16 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    
    if (power_arc_timer_cnt >= total_cnt_max) {
        power_arc_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * entrance
 * Component: power_arc
 * Mode: Preset actions (multi-segment animation)
 * Segments: 5
 */
void power_arc_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 164;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 97;
    const uint16_t seg1_start = 97;
    const uint16_t seg1_end = 113;
    const uint16_t seg2_start = 113;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 163;
    const uint16_t seg4_start = 163;
    const uint16_t seg4_end = 164;
    
    power_arc_timer_cnt++;
    
    // Segment 1: Wait 2900ms
    if (power_arc_timer_cnt > seg0_start && power_arc_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 480ms, 1 action(s)
    else if (power_arc_timer_cnt > seg1_start && power_arc_timer_cnt <= seg1_end) {
        uint16_t seg_cnt = power_arc_timer_cnt - seg1_start;
        const uint16_t seg_cnt_max = seg1_end - seg1_start;
        
            // Image sequence animation: 16 images
            const void *img_data_array[16] = {
                "/resource/power_arc/power_arc_01.bin",
                "/resource/power_arc/power_arc_02.bin",
                "/resource/power_arc/power_arc_03.bin",
                "/resource/power_arc/power_arc_04.bin",
                "/resource/power_arc/power_arc_05.bin",
                "/resource/power_arc/power_arc_06.bin",
                "/resource/power_arc/power_arc_07.bin",
                "/resource/power_arc/power_arc_08.bin",
                "/resource/power_arc/power_arc_09.bin",
                "/resource/power_arc/power_arc_10.bin",
                "/resource/power_arc/power_arc_11.bin",
                "/resource/power_arc/power_arc_12.bin",
                "/resource/power_arc/power_arc_13.bin",
                "/resource/power_arc/power_arc_14.bin",
                "/resource/power_arc/power_arc_15.bin",
                "/resource/power_arc/power_arc_16.bin"
            };
            uint16_t index = (16 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    // Segment 3: 480ms, 1 action(s)
    else if (power_arc_timer_cnt > seg2_start && power_arc_timer_cnt <= seg2_end) {
        uint16_t seg_cnt = power_arc_timer_cnt - seg2_start;
        const uint16_t seg_cnt_max = seg2_end - seg2_start;
        
            // Image sequence animation: 16 images
            const void *img_data_array[16] = {
                "/resource/power_arc/power_arc_15.bin",
                "/resource/power_arc/power_arc_14.bin",
                "/resource/power_arc/power_arc_13.bin",
                "/resource/power_arc/power_arc_12.bin",
                "/resource/power_arc/power_arc_11.bin",
                "/resource/power_arc/power_arc_10.bin",
                "/resource/power_arc/power_arc_09.bin",
                "/resource/power_arc/power_arc_08.bin",
                "/resource/power_arc/power_arc_07.bin",
                "/resource/power_arc/power_arc_06.bin",
                "/resource/power_arc/power_arc_05.bin",
                "/resource/power_arc/power_arc_04.bin",
                "/resource/power_arc/power_arc_03.bin",
                "/resource/power_arc/power_arc_02.bin",
                "/resource/power_arc/power_arc_01.bin",
                "/resource/power_arc/power_arc_00.bin"
            };
            uint16_t index = (16 - 1) * seg_cnt / seg_cnt_max;
            gui_img_set_src((gui_img_t *)target, img_data_array[index], IMG_SRC_FILESYS);
            gui_img_refresh_size((gui_img_t *)target);
            
    }
    // Segment 4: Wait 1000ms
    else if (power_arc_timer_cnt > seg3_start && power_arc_timer_cnt <= seg3_end) {
        // No action, just wait
    }
    // Segment 5: 30ms, 1 action(s)
    else if (power_arc_timer_cnt > seg4_start && power_arc_timer_cnt <= seg4_end) {
            // Timer control
            // Start timer animation: arc_animate
            power_arc_timer_cnt = 0; // Reset counter
            gui_obj_create_timer(target, 100, true, power_arc_timer_0_cb);
            gui_obj_start_timer(target);
            return; // Return immediately after timer control
            
    }
    
    if (power_arc_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        power_arc_timer_cnt = 0; // Reset counter
    }
}


/**
 * rotate
 * Component: dail_pointer_p
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void dail_pointer_p_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 16;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 16;
    
    dail_pointer_p_timer_cnt++;
    
    // Segment 1: 1600ms, 1 action(s)
    if (dail_pointer_p_timer_cnt > seg0_start && dail_pointer_p_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = dail_pointer_p_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust rotation: 0° -> -360°
            const float angle_origin = 0;
            const float angle_target = -360;
            float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
            gui_img_rotation((gui_img_t *)target, angle_cur);
            
    }
    
    if (dail_pointer_p_timer_cnt >= total_cnt_max) {
        dail_pointer_p_timer_cnt = 0; // Reset counter, continue loop
    }
}


/**
 * entrance
 * Component: dail_pointer_p
 * Mode: Preset actions (multi-segment animation)
 * Segments: 5
 */
void dail_pointer_p_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 164;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 97;
    const uint16_t seg1_start = 97;
    const uint16_t seg1_end = 113;
    const uint16_t seg2_start = 113;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 163;
    const uint16_t seg4_start = 163;
    const uint16_t seg4_end = 164;
    
    dail_pointer_p_timer_cnt++;
    
    // Segment 1: Wait 2900ms
    if (dail_pointer_p_timer_cnt > seg0_start && dail_pointer_p_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 480ms, 1 action(s)
    else if (dail_pointer_p_timer_cnt > seg1_start && dail_pointer_p_timer_cnt <= seg1_end) {
        uint16_t seg_cnt = dail_pointer_p_timer_cnt - seg1_start;
        const uint16_t seg_cnt_max = seg1_end - seg1_start;
        
            // Adjust rotation: 0° -> -360°
            const float angle_origin = 0;
            const float angle_target = -360;
            float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
            gui_img_rotation((gui_img_t *)target, angle_cur);
            
    }
    // Segment 3: 480ms, 1 action(s)
    else if (dail_pointer_p_timer_cnt > seg2_start && dail_pointer_p_timer_cnt <= seg2_end) {
        uint16_t seg_cnt = dail_pointer_p_timer_cnt - seg2_start;
        const uint16_t seg_cnt_max = seg2_end - seg2_start;
        
            // Adjust rotation: 0° -> 360°
            const float angle_origin = 0;
            const float angle_target = 360;
            float angle_cur = angle_origin + (angle_target - angle_origin) * seg_cnt / seg_cnt_max;
            gui_img_rotation((gui_img_t *)target, angle_cur);
            
    }
    // Segment 4: Wait 1000ms
    else if (dail_pointer_p_timer_cnt > seg3_start && dail_pointer_p_timer_cnt <= seg3_end) {
        // No action, just wait
    }
    // Segment 5: 30ms, 1 action(s)
    else if (dail_pointer_p_timer_cnt > seg4_start && dail_pointer_p_timer_cnt <= seg4_end) {
            // Timer control
            // Start timer animation: rotate
            dail_pointer_p_timer_cnt = 0; // Reset counter
            gui_obj_create_timer(target, 100, true, dail_pointer_p_timer_0_cb);
            gui_obj_start_timer(target);
            return; // Return immediately after timer control
            
    }
    
    if (dail_pointer_p_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        dail_pointer_p_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: win_common
 * Mode: Preset actions (multi-segment animation)
 * Segments: 2
 */
void win_common_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 100;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 99;
    const uint16_t seg1_start = 99;
    const uint16_t seg1_end = 100;
    
    win_common_timer_cnt++;
    
    // Segment 1: Wait 1980ms
    if (win_common_timer_cnt > seg0_start && win_common_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 20ms, 1 action(s)
    else if (win_common_timer_cnt > seg1_start && win_common_timer_cnt <= seg1_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
    }
    
    if (win_common_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_common_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: turn_l_icon
 * Mode: Preset actions (multi-segment animation)
 * Segments: 8
 */
void turn_l_icon_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 159;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 67;
    const uint16_t seg1_start = 67;
    const uint16_t seg1_end = 77;
    const uint16_t seg2_start = 77;
    const uint16_t seg2_end = 87;
    const uint16_t seg3_start = 87;
    const uint16_t seg3_end = 97;
    const uint16_t seg4_start = 97;
    const uint16_t seg4_end = 129;
    const uint16_t seg5_start = 129;
    const uint16_t seg5_end = 139;
    const uint16_t seg6_start = 139;
    const uint16_t seg6_end = 149;
    const uint16_t seg7_start = 149;
    const uint16_t seg7_end = 159;
    
    turn_l_icon_timer_cnt++;
    
    // Segment 1: Wait 2000ms
    if (turn_l_icon_timer_cnt > seg0_start && turn_l_icon_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (turn_l_icon_timer_cnt > seg1_start && turn_l_icon_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFF10995E
            gui_img_a8_recolor((gui_img_t *)target, 0xFF10995E);
            
    }
    // Segment 3: 300ms, 1 action(s)
    else if (turn_l_icon_timer_cnt > seg2_start && turn_l_icon_timer_cnt <= seg2_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    // Segment 4: 300ms, 1 action(s)
    else if (turn_l_icon_timer_cnt > seg3_start && turn_l_icon_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFF10995E
            gui_img_a8_recolor((gui_img_t *)target, 0xFF10995E);
            
    }
    // Segment 5: Wait 960ms
    else if (turn_l_icon_timer_cnt > seg4_start && turn_l_icon_timer_cnt <= seg4_end) {
        // No action, just wait
    }
    // Segment 6: 300ms, 1 action(s)
    else if (turn_l_icon_timer_cnt > seg5_start && turn_l_icon_timer_cnt <= seg5_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    // Segment 7: 300ms, 1 action(s)
    else if (turn_l_icon_timer_cnt > seg6_start && turn_l_icon_timer_cnt <= seg6_end) {
            // Set foreground color: 0xFF10995E
            gui_img_a8_recolor((gui_img_t *)target, 0xFF10995E);
            
    }
    // Segment 8: 300ms, 1 action(s)
    else if (turn_l_icon_timer_cnt > seg7_start && turn_l_icon_timer_cnt <= seg7_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (turn_l_icon_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        turn_l_icon_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: turn_r_icon
 * Mode: Preset actions (multi-segment animation)
 * Segments: 8
 */
void turn_r_icon_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 159;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 67;
    const uint16_t seg1_start = 67;
    const uint16_t seg1_end = 77;
    const uint16_t seg2_start = 77;
    const uint16_t seg2_end = 87;
    const uint16_t seg3_start = 87;
    const uint16_t seg3_end = 97;
    const uint16_t seg4_start = 97;
    const uint16_t seg4_end = 129;
    const uint16_t seg5_start = 129;
    const uint16_t seg5_end = 139;
    const uint16_t seg6_start = 139;
    const uint16_t seg6_end = 149;
    const uint16_t seg7_start = 149;
    const uint16_t seg7_end = 159;
    
    turn_r_icon_timer_cnt++;
    
    // Segment 1: Wait 2000ms
    if (turn_r_icon_timer_cnt > seg0_start && turn_r_icon_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (turn_r_icon_timer_cnt > seg1_start && turn_r_icon_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFF10995E
            gui_img_a8_recolor((gui_img_t *)target, 0xFF10995E);
            
    }
    // Segment 3: 300ms, 1 action(s)
    else if (turn_r_icon_timer_cnt > seg2_start && turn_r_icon_timer_cnt <= seg2_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    // Segment 4: 300ms, 1 action(s)
    else if (turn_r_icon_timer_cnt > seg3_start && turn_r_icon_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFF10995E
            gui_img_a8_recolor((gui_img_t *)target, 0xFF10995E);
            
    }
    // Segment 5: Wait 960ms
    else if (turn_r_icon_timer_cnt > seg4_start && turn_r_icon_timer_cnt <= seg4_end) {
        // No action, just wait
    }
    // Segment 6: 300ms, 1 action(s)
    else if (turn_r_icon_timer_cnt > seg5_start && turn_r_icon_timer_cnt <= seg5_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    // Segment 7: 300ms, 1 action(s)
    else if (turn_r_icon_timer_cnt > seg6_start && turn_r_icon_timer_cnt <= seg6_end) {
            // Set foreground color: 0xFF10995E
            gui_img_a8_recolor((gui_img_t *)target, 0xFF10995E);
            
    }
    // Segment 8: 300ms, 1 action(s)
    else if (turn_r_icon_timer_cnt > seg7_start && turn_r_icon_timer_cnt <= seg7_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (turn_r_icon_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        turn_r_icon_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: led_0
 * Mode: Preset actions (multi-segment animation)
 * Segments: 4
 */
void led_0_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 139;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 87;
    const uint16_t seg1_start = 87;
    const uint16_t seg1_end = 97;
    const uint16_t seg2_start = 97;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 139;
    
    led_0_timer_cnt++;
    
    // Segment 1: Wait 2600ms
    if (led_0_timer_cnt > seg0_start && led_0_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (led_0_timer_cnt > seg1_start && led_0_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFFFFB53E
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFB53E);
            
    }
    // Segment 3: Wait 960ms
    else if (led_0_timer_cnt > seg2_start && led_0_timer_cnt <= seg2_end) {
        // No action, just wait
    }
    // Segment 4: 300ms, 1 action(s)
    else if (led_0_timer_cnt > seg3_start && led_0_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (led_0_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        led_0_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: led_1
 * Mode: Preset actions (multi-segment animation)
 * Segments: 4
 */
void led_1_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 149;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 77;
    const uint16_t seg1_start = 77;
    const uint16_t seg1_end = 87;
    const uint16_t seg2_start = 87;
    const uint16_t seg2_end = 139;
    const uint16_t seg3_start = 139;
    const uint16_t seg3_end = 149;
    
    led_1_timer_cnt++;
    
    // Segment 1: Wait 2300ms
    if (led_1_timer_cnt > seg0_start && led_1_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (led_1_timer_cnt > seg1_start && led_1_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFFFFB53E
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFB53E);
            
    }
    // Segment 3: Wait 1560ms
    else if (led_1_timer_cnt > seg2_start && led_1_timer_cnt <= seg2_end) {
        // No action, just wait
    }
    // Segment 4: 300ms, 1 action(s)
    else if (led_1_timer_cnt > seg3_start && led_1_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (led_1_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        led_1_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: led_2
 * Mode: Preset actions (multi-segment animation)
 * Segments: 4
 */
void led_2_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 159;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 67;
    const uint16_t seg1_start = 67;
    const uint16_t seg1_end = 77;
    const uint16_t seg2_start = 77;
    const uint16_t seg2_end = 149;
    const uint16_t seg3_start = 149;
    const uint16_t seg3_end = 159;
    
    led_2_timer_cnt++;
    
    // Segment 1: Wait 2000ms
    if (led_2_timer_cnt > seg0_start && led_2_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (led_2_timer_cnt > seg1_start && led_2_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFFFFB53E
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFB53E);
            
    }
    // Segment 3: Wait 2160ms
    else if (led_2_timer_cnt > seg2_start && led_2_timer_cnt <= seg2_end) {
        // No action, just wait
    }
    // Segment 4: 300ms, 1 action(s)
    else if (led_2_timer_cnt > seg3_start && led_2_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (led_2_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        led_2_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: led_3
 * Mode: Preset actions (multi-segment animation)
 * Segments: 4
 */
void led_3_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 139;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 87;
    const uint16_t seg1_start = 87;
    const uint16_t seg1_end = 97;
    const uint16_t seg2_start = 97;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 139;
    
    led_3_timer_cnt++;
    
    // Segment 1: Wait 2600ms
    if (led_3_timer_cnt > seg0_start && led_3_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (led_3_timer_cnt > seg1_start && led_3_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFFFFB53E
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFB53E);
            
    }
    // Segment 3: Wait 960ms
    else if (led_3_timer_cnt > seg2_start && led_3_timer_cnt <= seg2_end) {
        // No action, just wait
    }
    // Segment 4: 300ms, 1 action(s)
    else if (led_3_timer_cnt > seg3_start && led_3_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (led_3_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        led_3_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: led_4
 * Mode: Preset actions (multi-segment animation)
 * Segments: 4
 */
void led_4_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 149;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 77;
    const uint16_t seg1_start = 77;
    const uint16_t seg1_end = 87;
    const uint16_t seg2_start = 87;
    const uint16_t seg2_end = 139;
    const uint16_t seg3_start = 139;
    const uint16_t seg3_end = 149;
    
    led_4_timer_cnt++;
    
    // Segment 1: Wait 2300ms
    if (led_4_timer_cnt > seg0_start && led_4_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (led_4_timer_cnt > seg1_start && led_4_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFFFFB53E
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFB53E);
            
    }
    // Segment 3: Wait 1560ms
    else if (led_4_timer_cnt > seg2_start && led_4_timer_cnt <= seg2_end) {
        // No action, just wait
    }
    // Segment 4: 300ms, 1 action(s)
    else if (led_4_timer_cnt > seg3_start && led_4_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (led_4_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        led_4_timer_cnt = 0; // Reset counter
    }
}


/**
 * entrance
 * Component: led_5
 * Mode: Preset actions (multi-segment animation)
 * Segments: 4
 */
void led_5_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 159;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 67;
    const uint16_t seg1_start = 67;
    const uint16_t seg1_end = 77;
    const uint16_t seg2_start = 77;
    const uint16_t seg2_end = 149;
    const uint16_t seg3_start = 149;
    const uint16_t seg3_end = 159;
    
    led_5_timer_cnt++;
    
    // Segment 1: Wait 2000ms
    if (led_5_timer_cnt > seg0_start && led_5_timer_cnt <= seg0_end) {
        // No action, just wait
    }
    // Segment 2: 300ms, 1 action(s)
    else if (led_5_timer_cnt > seg1_start && led_5_timer_cnt <= seg1_end) {
            // Set foreground color: 0xFFFFB53E
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFB53E);
            
    }
    // Segment 3: Wait 2160ms
    else if (led_5_timer_cnt > seg2_start && led_5_timer_cnt <= seg2_end) {
        // No action, just wait
    }
    // Segment 4: 300ms, 1 action(s)
    else if (led_5_timer_cnt > seg3_start && led_5_timer_cnt <= seg3_end) {
            // Set foreground color: 0xFFFFFFFF
            gui_img_a8_recolor((gui_img_t *)target, 0xFFFFFFFF);
            
    }
    
    if (led_5_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        led_5_timer_cnt = 0; // Reset counter
    }
}


/**
 * pos func
 * Component: win_speed_text
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_speed_text_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_speed_text_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_speed_text_timer_cnt > seg0_start && win_speed_text_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = win_speed_text_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (38, 64) -> (38, -30)
            const int16_t x_origin = 38;
            const int16_t y_origin = 64;
            const int16_t x_target = 38;
            const int16_t y_target = -30;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (win_speed_text_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_speed_text_timer_cnt = 0; // Reset counter
    }
}


/**
 * pos default
 * Component: win_speed_text
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_speed_text_timer_2_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_speed_text_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_speed_text_timer_cnt > seg0_start && win_speed_text_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = win_speed_text_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (38, -30) -> (38, 64)
            const int16_t x_origin = 38;
            const int16_t y_origin = -30;
            const int16_t x_target = 38;
            const int16_t y_target = 64;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (win_speed_text_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_speed_text_timer_cnt = 0; // Reset counter
    }
}


/**
 * func pos
 * Component: win_power_text
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_power_text_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_power_text_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_power_text_timer_cnt > seg0_start && win_power_text_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = win_power_text_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (470, 64) -> (470, -30)
            const int16_t x_origin = 470;
            const int16_t y_origin = 64;
            const int16_t x_target = 470;
            const int16_t y_target = -30;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (win_power_text_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_power_text_timer_cnt = 0; // Reset counter
    }
}


/**
 * default pos
 * Component: win_power_text
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_power_text_timer_2_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_power_text_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_power_text_timer_cnt > seg0_start && win_power_text_timer_cnt <= seg0_end) {
        uint16_t seg_cnt = win_power_text_timer_cnt - seg0_start;
        const uint16_t seg_cnt_max = seg0_end - seg0_start;
        
            // Adjust position: (470, -30) -> (470, 64)
            const int16_t x_origin = 470;
            const int16_t y_origin = -30;
            const int16_t x_target = 470;
            const int16_t y_target = 64;
            int16_t x_cur = x_origin + (x_target - x_origin) * seg_cnt / seg_cnt_max;
            int16_t y_cur = y_origin + (y_target - y_origin) * seg_cnt / seg_cnt_max;
            gui_obj_move(target, x_cur, y_cur);
            
    }
    
    if (win_power_text_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_power_text_timer_cnt = 0; // Reset counter
    }
}


/**
 * show win_func
 * Component: win_func
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_func_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_func_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_func_timer_cnt > seg0_start && win_func_timer_cnt <= seg0_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
    }
    
    if (win_func_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_func_timer_cnt = 0; // Reset counter
    }
}


/**
 * hide win_func
 * Component: win_func
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_func_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_func_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_func_timer_cnt > seg0_start && win_func_timer_cnt <= seg0_end) {
            // Set visibility: hide
            gui_obj_show(target, false);
            
    }
    
    if (win_func_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_func_timer_cnt = 0; // Reset counter
    }
}


/**
 * show
 * Component: win_app_list
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_app_list_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_app_list_timer_cnt++;
    
    // Segment 1: 10ms, 2 action(s)
    if (win_app_list_timer_cnt > seg0_start && win_app_list_timer_cnt <= seg0_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
            // Set focus
            gui_obj_focus_set(target);
            
    }
    
    if (win_app_list_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_app_list_timer_cnt = 0; // Reset counter
    }
}


/**
 * hide
 * Component: win_app_list
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_app_list_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_app_list_timer_cnt++;
    
    // Segment 1: 10ms, 1 action(s)
    if (win_app_list_timer_cnt > seg0_start && win_app_list_timer_cnt <= seg0_end) {
            // Set visibility: hide
            gui_obj_show(target, false);
            
    }
    
    if (win_app_list_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_app_list_timer_cnt = 0; // Reset counter
    }
}


/**
 * show
 * Component: win_app_weather
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_app_weather_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_app_weather_timer_cnt++;
    
    // Segment 1: 20ms, 2 action(s)
    if (win_app_weather_timer_cnt > seg0_start && win_app_weather_timer_cnt <= seg0_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
            // Set focus
            gui_obj_focus_set(target);
            
    }
    
    if (win_app_weather_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_app_weather_timer_cnt = 0; // Reset counter
    }
}


/**
 * hide
 * Component: win_app_weather
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_app_weather_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 2;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 2;
    
    win_app_weather_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_app_weather_timer_cnt > seg0_start && win_app_weather_timer_cnt <= seg0_end) {
            // Set visibility: hide
            gui_obj_show(target, false);
            
    }
    
    if (win_app_weather_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_app_weather_timer_cnt = 0; // Reset counter
    }
}


/**
 * show
 * Component: win_app_developing
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_app_developing_timer_0_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_app_developing_timer_cnt++;
    
    // Segment 1: 20ms, 2 action(s)
    if (win_app_developing_timer_cnt > seg0_start && win_app_developing_timer_cnt <= seg0_end) {
            // Set visibility: show
            gui_obj_show(target, true);
            
            // Set focus
            gui_obj_focus_set(target);
            
    }
    
    if (win_app_developing_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_app_developing_timer_cnt = 0; // Reset counter
    }
}


/**
 * hide
 * Component: win_app_developing
 * Mode: Preset actions (multi-segment animation)
 * Segments: 1
 */
void win_app_developing_timer_1_cb(void *obj)
{
    gui_obj_t *target = (gui_obj_t *)obj;
    const uint16_t total_cnt_max = 1;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 1;
    
    win_app_developing_timer_cnt++;
    
    // Segment 1: 20ms, 1 action(s)
    if (win_app_developing_timer_cnt > seg0_start && win_app_developing_timer_cnt <= seg0_end) {
            // Set visibility: hide
            gui_obj_show(target, false);
            
    }
    
    if (win_app_developing_timer_cnt >= total_cnt_max) {
        gui_obj_stop_timer(target);
        win_app_developing_timer_cnt = 0; // Reset counter
    }
}


/* @protected start custom_functions */
// Custom functions
char speed_str[4] = {0};
uint8_t speed_val = 0;
void text_speed_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    speed_val += 10;
    speed_val %= 160;
    sprintf(speed_str, "%d", speed_val);
    gui_text_content_set(obj, speed_str, strlen(speed_str));
}

void text_speed_timer_1_cb(void *obj)
{
    GUI_UNUSED(obj);
    static uint16_t cnt = 0;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 97;
    const uint16_t seg1_start = 97;
    const uint16_t seg1_end = 113;
    const uint16_t seg2_start = 113;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 163;
    const uint16_t seg4_start = 163;
    const uint16_t seg4_end = 164;
    
    cnt++;
    
    if (cnt > seg0_start && cnt <= seg0_end) {
        // wait
        speed_val = 0;
    }
    else if (cnt > seg1_start && cnt <= seg1_end) {
        speed_val += 10;
    }
    else if (cnt > seg2_start && cnt <= seg2_end) {
        speed_val -= 10;
    }
    else if (cnt > seg3_start && cnt <= seg3_end) {
        // wait
    }
    else if (cnt > seg4_start && cnt <= seg4_end) {
            gui_obj_create_timer(obj, 100, true, text_speed_timer_0_cb);
            gui_obj_start_timer(obj);
            cnt = 0;
            return;
            
    }
    sprintf(speed_str, "%d", speed_val);
    gui_text_content_set(obj, speed_str, strlen(speed_str));
}

char power_str[4] = {0}; 
uint8_t power_val = 0;
void text_power_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    power_val += 10;
    power_val %= 160;
    sprintf(power_str, "%d", power_val);
    gui_text_content_set(obj, power_str, strlen(power_str));
}

void text_power_timer_1_cb(void *obj)
{
    GUI_UNUSED(obj);
    static uint16_t cnt = 0;
    
    const uint16_t seg0_start = 0;
    const uint16_t seg0_end = 97;
    const uint16_t seg1_start = 97;
    const uint16_t seg1_end = 113;
    const uint16_t seg2_start = 113;
    const uint16_t seg2_end = 129;
    const uint16_t seg3_start = 129;
    const uint16_t seg3_end = 163;
    const uint16_t seg4_start = 163;
    const uint16_t seg4_end = 164;
    
    cnt++;
    
    if (cnt > seg0_start && cnt <= seg0_end) {
        // wait
        power_val = 0;
    }
    else if (cnt > seg1_start && cnt <= seg1_end) {
        power_val += 10;
    }
    else if (cnt > seg2_start && cnt <= seg2_end) {
        power_val -= 10;
    }
    else if (cnt > seg3_start && cnt <= seg3_end) {
        // wait
    }
    else if (cnt > seg4_start && cnt <= seg4_end) {
            gui_obj_create_timer(obj, 100, true, text_power_timer_0_cb);
            gui_obj_start_timer(obj);
            cnt = 0;
            return;
            
    }
    sprintf(power_str, "%d", power_val);
    gui_text_content_set(obj, power_str, strlen(power_str));
}

static char time_str[8] = "00:00";
void text_time_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    time_t rawtime;
    time(&rawtime);
    struct tm *timeinfo = localtime(&rawtime);
    if (timeinfo)
    {
        sprintf(time_str, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    }
    gui_text_content_set(obj, time_str, strlen(time_str));
}

void zoom_animation(gui_obj_t *obj, float scale)
{
    matrix_translate(obj->w / 2, obj->h / 2, obj->matrix);
    if (scale == 0)
    {
        matrix_scale(1.f, 1.f, obj->matrix);
    }
    else
    {
        matrix_scale(scale, scale, obj->matrix);
    }
    matrix_translate(obj->w / -2, obj->h / -2, obj->matrix);
}

void win_speed_text_timer_0_cb(void *obj)
{
    static uint16_t cnt = 0;
    uint16_t cnt_max = 50;
    cnt++;
    float scale = (float)cnt / cnt_max;
    zoom_animation(obj, scale);
    if (cnt >= cnt_max)
    {
        cnt = 0;
        gui_obj_stop_timer(obj);
    }
}

void win_power_text_timer_0_cb(void *obj)
{
    static uint16_t cnt = 0;
    uint16_t cnt_max = 50;
    cnt++;
    float scale = (float)cnt / cnt_max;
    zoom_animation(obj, scale);
    if (cnt >= cnt_max)
    {
        cnt = 0;
        gui_obj_stop_timer(obj);
    }
}

uint8_t app_index = 0;

void tab_hightlight_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    gui_obj_t *target = obj;
    int16_t y = 0;
    switch (app_index)
    {
    case 1:
        y = 90;
        break;
    case 2:
        y = 180;
        break;
    case 4:
        y = 90;
        break;
    case 5:
        y = 180;
        break;
    default:
        break;
    }
    gui_obj_move(target, target->x, y);
}

void app_list_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    gui_list_set_offset(obj, 0);
    gui_obj_stop_timer(obj);
}

void app_list_timer_1_cb(void *obj)
{
    GUI_UNUSED(obj);
    gui_list_set_offset(obj, -280);
    gui_obj_stop_timer(obj);
}

uint8_t weather_note_index = 0;
void list_weather_timer_0_cb(void *obj)
{
    GUI_UNUSED(obj);
    gui_list_set_offset(obj, weather_note_index * -280);
    gui_obj_stop_timer(obj);
}
/* @protected end custom_functions */
