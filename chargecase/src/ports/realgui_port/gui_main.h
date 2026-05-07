/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef __GUI_MAIN_H__
#define __GUI_MAIN_H__
#ifdef __cplusplus
extern "C" {
#endif
extern void gui_port_dc_init(void);
extern void gui_port_indev_init(void);
extern void gui_port_os_init(void);
/*****launch interface*****/

extern int rt_gui_demo_init(void);
extern int rtgui_server_init(void);
extern int app_launcher_install(void);
extern int app_launcher_startup(void);
extern int app_dialing_install(void);
extern int app_setting_install(void);
extern int app_album_install(void);
extern int app_game_install(void);
int gui_main(void);
#ifdef __cplusplus
}
#endif

#endif
