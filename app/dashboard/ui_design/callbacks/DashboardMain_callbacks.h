#ifndef DASHBOARDMAIN_CALLBACKS_H
#define DASHBOARDMAIN_CALLBACKS_H

#include "gui_api.h"
#include "gui_text.h"
#include "gui_obj_focus.h"

// Timer animation counters (defined in callbacks.c)
extern uint16_t gloom_timer_cnt;
extern uint16_t shadow_timer_cnt;
extern uint16_t win_map_timer_cnt;
extern uint16_t icon_dir_timer_cnt;
extern uint16_t map_timer_cnt;
extern uint16_t root_menu_timer_cnt;
extern uint16_t win_line_timer_cnt;
extern uint16_t line_left_timer_cnt;
extern uint16_t line_right_timer_cnt;
extern uint16_t win_dail_timer_cnt;
extern uint16_t speed_arc_timer_cnt;
extern uint16_t dail_pointer_s_timer_cnt;
extern uint16_t power_arc_timer_cnt;
extern uint16_t dail_pointer_p_timer_cnt;
extern uint16_t win_common_timer_cnt;
extern uint16_t turn_l_icon_timer_cnt;
extern uint16_t turn_r_icon_timer_cnt;
extern uint16_t text_time_timer_cnt;
extern uint16_t led_0_timer_cnt;
extern uint16_t led_1_timer_cnt;
extern uint16_t led_2_timer_cnt;
extern uint16_t led_3_timer_cnt;
extern uint16_t led_4_timer_cnt;
extern uint16_t led_5_timer_cnt;
extern uint16_t win_speed_text_timer_cnt;
extern uint16_t text_speed_timer_cnt;
extern uint16_t win_power_text_timer_cnt;
extern uint16_t text_power_timer_cnt;
extern uint16_t win_func_timer_cnt;
extern uint16_t win_app_list_timer_cnt;
extern uint16_t app_list_timer_cnt;
extern uint16_t tab_hightlight_timer_cnt;
extern uint16_t win_app_weather_timer_cnt;
extern uint16_t list_weather_timer_cnt;
extern uint16_t win_app_developing_timer_cnt;

// Event callback function declarations
void dail_power_clicked_cb(void *obj, gui_event_t *e);
void off_icon_clicked_cb(void *obj, gui_event_t *e);
void root_menu_key_cb(void *obj, gui_event_t *e);
void start_engine_key_cb(void *obj, gui_event_t *e);
void start_engine_pressed_cb(void *obj, gui_event_t *e);
void start_engine_released_cb(void *obj, gui_event_t *e);
void win_app_developing_key_0_cb(void *obj, gui_event_t *e);
void win_app_list_key_0_cb(void *obj, gui_event_t *e);
void win_app_weather_key_0_cb(void *obj, gui_event_t *e);

// User-configured timer callback function declarations
void gloom_timer_0_cb(void *obj);
void shadow_timer_0_cb(void *obj);
void shadow_timer_1_cb(void *obj);
void win_map_timer_0_cb(void *obj);
void win_map_timer_1_cb(void *obj);
void win_map_timer_2_cb(void *obj);
void icon_dir_timer_0_cb(void *obj);
void map_timer_0_cb(void *obj);
void map_timer_1_cb(void *obj);
void root_menu_timer_0_cb(void *obj);
void root_menu_timer_1_cb(void *obj);
void root_menu_timer_2_cb(void *obj);
void root_menu_timer_3_cb(void *obj);
void root_menu_timer_4_cb(void *obj);
void root_menu_timer_5_cb(void *obj);
void root_menu_timer_6_cb(void *obj);
void root_menu_timer_7_cb(void *obj);
void root_menu_timer_8_cb(void *obj);
void root_menu_timer_9_cb(void *obj);
void root_menu_timer_10_cb(void *obj);
void root_menu_timer_11_cb(void *obj);
void win_line_timer_0_cb(void *obj);
void line_left_timer_0_cb(void *obj);
void line_right_timer_0_cb(void *obj);
void win_dail_timer_0_cb(void *obj);
void win_dail_timer_1_cb(void *obj);
void win_dail_timer_2_cb(void *obj);
void speed_arc_timer_0_cb(void *obj);
void speed_arc_timer_1_cb(void *obj);
void dail_pointer_s_timer_0_cb(void *obj);
void dail_pointer_s_timer_1_cb(void *obj);
void power_arc_timer_0_cb(void *obj);
void power_arc_timer_1_cb(void *obj);
void dail_pointer_p_timer_0_cb(void *obj);
void dail_pointer_p_timer_1_cb(void *obj);
void win_common_timer_0_cb(void *obj);
void turn_l_icon_timer_0_cb(void *obj);
void turn_r_icon_timer_0_cb(void *obj);
void text_time_timer_0_cb(void *obj);
void led_0_timer_0_cb(void *obj);
void led_1_timer_0_cb(void *obj);
void led_2_timer_0_cb(void *obj);
void led_3_timer_0_cb(void *obj);
void led_4_timer_0_cb(void *obj);
void led_5_timer_0_cb(void *obj);
void win_speed_text_timer_0_cb(void *obj);
void win_speed_text_timer_1_cb(void *obj);
void win_speed_text_timer_2_cb(void *obj);
void text_speed_timer_0_cb(void *obj);
void text_speed_timer_1_cb(void *obj);
void win_power_text_timer_0_cb(void *obj);
void win_power_text_timer_1_cb(void *obj);
void win_power_text_timer_2_cb(void *obj);
void text_power_timer_0_cb(void *obj);
void text_power_timer_1_cb(void *obj);
void win_func_timer_0_cb(void *obj);
void win_func_timer_1_cb(void *obj);
void win_app_list_timer_0_cb(void *obj);
void win_app_list_timer_1_cb(void *obj);
void app_list_timer_0_cb(void *obj);
void app_list_timer_1_cb(void *obj);
void tab_hightlight_timer_0_cb(void *obj);
void win_app_weather_timer_0_cb(void *obj);
void win_app_weather_timer_1_cb(void *obj);
void list_weather_timer_0_cb(void *obj);
void win_app_developing_timer_0_cb(void *obj);
void win_app_developing_timer_1_cb(void *obj);

// Custom function declarations (auto-extracted from callbacks.c protected area)
void text_speed_timer_0_cb(void *obj);
void text_speed_timer_1_cb(void *obj);
void text_power_timer_0_cb(void *obj);
void text_power_timer_1_cb(void *obj);
void text_time_timer_0_cb(void *obj);
void zoom_animation(gui_obj_t *obj, float scale);
void win_speed_text_timer_0_cb(void *obj);
void win_power_text_timer_0_cb(void *obj);
void tab_hightlight_timer_0_cb(void *obj);
void app_list_timer_0_cb(void *obj);
void app_list_timer_1_cb(void *obj);
void list_weather_timer_0_cb(void *obj);

#endif // DASHBOARDMAIN_CALLBACKS_H
