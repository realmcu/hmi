/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * Licensed under the Realtek License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License from Realtek
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* ============================================================================
 * Bridge: img_rx (TCP + frame pool) -> HoneyGUI carplay_map widget
 *
 *   rx thread: frame done -> publish_frame()
 *     -> g_notify=on_frame_ready() -> gui_send_msg_to_server(USER_DEFINE)
 *   GUI thread: gui_recv_msg_to_server() -> carplay_map_apply_cb()
 *     -> take_display() -> gui_img_set_src() -> gui_fb_change()
 * ============================================================================ */

#include "gui_message.h"
#include "gui_img.h"
#include "gui_view.h"
#include "gui_components_init.h"

#include "dashboard_img_rx.h"

extern void gui_fb_change(void);

/* carplay_map: created by DashboardMain_ui.c, NULL when view switched out. */
extern gui_img_t *carplay_map;

static void carplay_map_apply_cb(void *param)
{
	(void)param;

	if (carplay_map == NULL) {
		gui_view_switch_direct(gui_view_get_current(), "carplay_view",
		                       SWITCH_OUT_NONE_ANIMATION, SWITCH_IN_NONE_ANIMATION);
		if (carplay_map == NULL) {
			return;
		}
	}

	const uint8_t *buf = dashboard_img_rx_take_display();
	if (buf == NULL) {
		return;
	}

	gui_img_set_src(carplay_map, buf, IMG_SRC_MEMADDR);
	gui_img_refresh_size(carplay_map);
	gui_fb_change();
}

static void on_frame_ready(void)
{
	gui_msg_t msg = {
		.event = GUI_EVENT_USER_DEFINE,
		.cb    = carplay_map_apply_cb,
	};
	gui_send_msg_to_server(&msg);
}

static int dashboard_img_display_init(void)
{
	dashboard_img_rx_register_notify(on_frame_ready);
	return 0;
}
GUI_INIT_APP_EXPORT(dashboard_img_display_init);
