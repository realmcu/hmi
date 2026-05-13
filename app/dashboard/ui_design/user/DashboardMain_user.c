#include "DashboardMain_user.h"

/**
 * User-defined implementation
 * This file is generated once only, feel free to modify
 */

// Msg callback stubs (called from DashboardMain_callbacks.c)
static bool menu_disp = false;
static uint8_t menu_func_index = 0;
extern uint8_t app_index;
extern uint8_t weather_note_index;
extern uint8_t speed_val;
extern uint8_t power_val;


void root_menu_msg_show_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    if (!menu_disp)
    {
        gui_obj_create_timer(GUI_BASE(root_menu), 10, true, root_menu_timer_1_cb);
        gui_obj_start_timer(GUI_BASE(root_menu));
        menu_disp = true;
    }
    else
    {
        void *cb = NULL;
        switch (menu_func_index)
        {
        case 0:
            cb = root_menu_timer_3_cb;
            break;
        case 1:
            cb = root_menu_timer_4_cb;
            break;
        case 2:
            cb = root_menu_timer_5_cb;
            break;
        case 3:
            cb = root_menu_timer_6_cb;
            break;
        default:
            break;
        }
        menu_func_index++;
        menu_func_index %= 4;
        gui_obj_create_timer(GUI_BASE(root_menu), 10, true, cb);
        gui_obj_start_timer(GUI_BASE(root_menu));
    }
}

void root_menu_msg_enter_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show || !menu_disp) return;
    
    switch (menu_func_index)
    {
    case 0:
        gui_obj_create_timer(GUI_BASE(win_map), 10, true, win_map_timer_1_cb);
        gui_obj_start_timer(GUI_BASE(win_map));
        gui_obj_create_timer(GUI_BASE(win_dail), 10, true, win_dail_timer_1_cb);
        gui_obj_start_timer(GUI_BASE(win_dail));
        gui_obj_create_timer(GUI_BASE(win_speed_text), 10, true, win_speed_text_timer_2_cb);
        gui_obj_start_timer(GUI_BASE(win_speed_text));
        gui_obj_create_timer(GUI_BASE(win_power_text), 10, true, win_power_text_timer_2_cb);
        gui_obj_start_timer(GUI_BASE(win_power_text));
        GUI_BASE(obj)->y = 480;
        menu_disp = false;
        break;
    case 1:
        gui_obj_create_timer(GUI_BASE(win_func), 10, true, win_func_timer_0_cb);
        gui_obj_start_timer(GUI_BASE(win_func));
        gui_obj_create_timer(GUI_BASE(win_app_list), 10, true, win_app_list_timer_0_cb);
        gui_obj_start_timer(GUI_BASE(win_app_list));
        gui_obj_create_timer(GUI_BASE(win_map), 10, true, win_map_timer_2_cb);
        gui_obj_start_timer(GUI_BASE(win_map));
        gui_obj_create_timer(GUI_BASE(win_dail), 10, true, win_dail_timer_2_cb);
        gui_obj_start_timer(GUI_BASE(win_dail));
        gui_obj_create_timer(GUI_BASE(win_speed_text), 10, true, win_speed_text_timer_1_cb);
        gui_obj_start_timer(GUI_BASE(win_speed_text));
        gui_obj_create_timer(GUI_BASE(win_power_text), 10, true, win_power_text_timer_1_cb);
        gui_obj_start_timer(GUI_BASE(win_power_text));
        GUI_BASE(obj)->y = 480;
        menu_disp = false;
        break;
    default:
        break;
    }
}

void root_menu_msg_hide_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    if (menu_disp)
    {
        gui_obj_create_timer(GUI_BASE(root_menu), 10, true, root_menu_timer_2_cb);
        gui_obj_start_timer(GUI_BASE(root_menu));
        menu_disp = false;
    }
}

void root_menu_msg_left_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    if (menu_disp)
    {
        void *cb = NULL;
        switch (menu_func_index)
        {
        case 0:
            cb = root_menu_timer_7_cb;
            break;
        case 3:
            cb = root_menu_timer_8_cb;
            break;
        case 2:
            cb = root_menu_timer_9_cb;
            break;
        case 1:
            cb = root_menu_timer_10_cb;
            break;
        default:
            break;
        }
        menu_func_index--;
        menu_func_index %= 4;
        gui_obj_create_timer(GUI_BASE(root_menu), 10, true, cb);
        gui_obj_start_timer(GUI_BASE(root_menu));
    }
}

void root_menu_msg_right_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    if (menu_disp)
    {
        void *cb = NULL;
        switch (menu_func_index)
        {
        case 0:
            cb = root_menu_timer_3_cb;
            break;
        case 1:
            cb = root_menu_timer_4_cb;
            break;
        case 2:
            cb = root_menu_timer_5_cb;
            break;
        case 3:
            cb = root_menu_timer_6_cb;
            break;
        default:
            break;
        }
        menu_func_index++;
        menu_func_index %= 4;
        gui_obj_create_timer(GUI_BASE(root_menu), 10, true, cb);
        gui_obj_start_timer(GUI_BASE(root_menu));
    }
}

void win_app_list_msg_enter_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    if (app_index == 3)
    {
        gui_obj_create_timer(GUI_BASE(win_app_weather), 10, true, win_app_weather_timer_0_cb);
        gui_obj_start_timer(GUI_BASE(win_app_weather));
    }
    else
    {
        gui_obj_create_timer(GUI_BASE(win_app_developing), 10, true, win_app_developing_timer_0_cb);
        gui_obj_start_timer(GUI_BASE(win_app_developing));
    }
    gui_obj_create_timer(GUI_BASE(win_app_list), 10, true, win_app_list_timer_1_cb);
    gui_obj_start_timer(GUI_BASE(win_app_list));
}

void win_app_list_msg_exit_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    app_index = 0;
    gui_obj_create_timer(GUI_BASE(win_func), 10, true, win_func_timer_1_cb);
    gui_obj_start_timer(GUI_BASE(win_func));
    gui_obj_create_timer(GUI_BASE(win_dail), 10, true, win_dail_timer_1_cb);
    gui_obj_start_timer(GUI_BASE(win_dail));
    gui_obj_create_timer(GUI_BASE(win_speed_text), 10, true, win_speed_text_timer_2_cb);
    gui_obj_start_timer(GUI_BASE(win_speed_text));
    gui_obj_create_timer(GUI_BASE(win_power_text), 10, true, win_power_text_timer_2_cb);
    gui_obj_start_timer(GUI_BASE(win_power_text));
    gui_obj_create_timer(GUI_BASE(root_menu), 10, true, root_menu_timer_11_cb);
    gui_obj_start_timer(GUI_BASE(root_menu));
    speed_arc_timer_cnt = 0;
    dail_pointer_s_timer_cnt = 0;
    power_arc_timer_cnt = 0;
    dail_pointer_p_timer_cnt = 0;
    speed_val = 0;
    power_val = 0;
}

void win_app_list_msg_last_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    if (app_index == 0) return;
    app_index--;
    if (app_index == 2)
    {
        gui_obj_create_timer(GUI_BASE(app_list), 10, true, app_list_timer_0_cb);
        gui_obj_start_timer(GUI_BASE(app_list));
    }
}

void win_app_list_msg_next_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    app_index++;
    if (app_index == 3)
    {
        gui_obj_create_timer(GUI_BASE(app_list), 10, true, app_list_timer_1_cb);
        gui_obj_start_timer(GUI_BASE(app_list));
    }
    else if (app_index == 6)
    {
        app_index = 0;
        gui_obj_create_timer(GUI_BASE(app_list), 10, true, app_list_timer_0_cb);
        gui_obj_start_timer(GUI_BASE(app_list));
    }
}

void win_app_developing_msg_exit_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    gui_obj_create_timer(GUI_BASE(win_app_list), 10, true, win_app_list_timer_0_cb);
    gui_obj_start_timer(GUI_BASE(win_app_list));
    gui_obj_create_timer(GUI_BASE(win_app_developing), 10, true, win_app_developing_timer_1_cb);
    gui_obj_start_timer(GUI_BASE(win_app_developing));
}

void win_app_weather_msg_next_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    weather_note_index++;
    weather_note_index %= 2;
    gui_obj_create_timer(GUI_BASE(list_weather), 10, true, list_weather_timer_0_cb);
    gui_obj_start_timer(GUI_BASE(list_weather));
}

void win_app_weather_msg_exit_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(obj);
    GUI_UNUSED(e);
    if (GUI_BASE(obj)->not_show) return;
    gui_obj_create_timer(GUI_BASE(win_app_list), 10, true, win_app_list_timer_0_cb);
    gui_obj_start_timer(GUI_BASE(win_app_list));
    gui_obj_create_timer(GUI_BASE(win_app_weather), 10, true, win_app_weather_timer_1_cb);
    gui_obj_start_timer(GUI_BASE(win_app_weather));
}

/***
 * Template function
 * Distinguish development environments
 */
// void user_defined_func_called_by_event(void *obj, gui_event_t *e)
// {
//     GUI_UNUSED(obj);
//     GUI_UNUSED(e);
// #ifdef _HONEYGUI_SIMULATOR_
//     // TODO
// #else
//     // TODO
// #endif
// }

// void user_defined_func_called_by_msg(void *obj, const char *topic, void *data, uint16_t len)
// {
//     GUI_UNUSED(obj);
//     GUI_UNUSED(topic);
//     GUI_UNUSED(data);
//     GUI_UNUSED(len);
// #ifdef _HONEYGUI_SIMULATOR_
//     // TODO
// #else
//     // TODO
// #endif
// }

// void list_note_design(void *obj, void *param)
// {
//     GUI_UNUSED(param);
//     // Cast obj to gui_list_note_t * type
//     gui_list_note_t *note = (gui_list_note_t *)obj;
//     uint16_t index = note->index;
//     GUI_UNUSED(index);
// }
