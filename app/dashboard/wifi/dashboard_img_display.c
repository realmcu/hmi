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
 * Dashboard image display glue: bridge between img_rx and HoneyGUI carplay_map
 *
 *   rx thread                          GUI thread (gui server)
 *   ─────────                         ─────────────────────
 *   frame complete → publish_frame()
 *      └─ g_notify = on_frame_ready()
 *            └─ gui_send_msg_to_server(USER_DEFINE, cb=carplay_map_apply_cb)
 *                                        │  (16-depth msg queue, cross-thread, no payload)
 *                                        ▼
 *                              gui_recv_msg_to_server()  ← called after each frame render
 *                                └─ gui_server_msg_handler(USER_DEFINE)
 *                                     └─ carplay_map_apply_cb()
 *                                          ├─ dashboard_img_rx_take_display()
 *                                          │     (take latest ready frame as display slot,
 *                                          │      previous display slot auto returns to free)
 *                                          ├─ gui_img_set_src(carplay_map, buf, MEMADDR)
 *                                          ├─ gui_img_refresh_size(carplay_map)
 *                                          └─ gui_fb_change()  → trigger next redraw
 *
 * take_display always gets the latest ready frame, so multiple events naturally merge.
 * The display slot being decoded/painted (g_display) is never touched by the rx thread.
 * ============================================================================ */

#include "gui_message.h"          /* gui_msg_t / GUI_EVENT_USER_DEFINE / gui_send_msg_to_server */
#include "gui_img.h"              /* gui_img_t / gui_img_set_src / gui_img_refresh_size / IMG_SRC_MEMADDR */
#include "gui_view.h"             /* gui_view_switch_direct / gui_view_get_current */
#include "gui_components_init.h"  /* GUI_INIT_APP_EXPORT */

#include "dashboard_img_rx.h"

/* Force redraw via extern (see example/map platform_honeygui.c). */
extern void gui_fb_change(void);

/* carplay_map is created by DashboardMain_ui.c in carplay_view_switch_in (global,
 * non-static). View switch_out sets it to NULL, so check for NULL here. */
extern gui_img_t *carplay_map;

/* ---------------------------------------------------------------------------
 * [GUI thread] USER_DEFINE event handler: update carplay_map image source
 * to the latest ready frame.
 *
 * If not on carplay page (carplay_map == NULL), auto-switch to carplay_view
 * (its switch_in will create carplay_map), then display the frame.
 * ------------------------------------------------------------------------- */
static void carplay_map_apply_cb(void *param)
{
	(void)param;                       /* event carries no payload, just take the latest frame */

	/* If not on carplay page: auto-switch (no animation). carplay_view's
	 * switch_in will create carplay_map. */
	if (carplay_map == NULL) {
		gui_view_switch_direct(gui_view_get_current(), "carplay_view",
		                       SWITCH_OUT_NONE_ANIMATION, SWITCH_IN_NONE_ANIMATION);
		/* After switching, carplay_map should have been created by switch_in */
		if (carplay_map == NULL) {
			return;
		}
	}

	/* Take latest ready frame: set as display slot, previous slot auto-returns to free.
	 * Returns a gui_jpeg_file_head_t layout buffer, usable directly as MEMADDR src. */
	const uint8_t *buf = dashboard_img_rx_take_display();
	if (buf == NULL) {
		return;                        /* no new frame (merged events) -- discard */
	}

	gui_img_set_src(carplay_map, buf, IMG_SRC_MEMADDR);   /* param is const uint8_t*, pass directly */
	gui_img_refresh_size(carplay_map);    /* refresh widget size from header dimensions */
	gui_fb_change();                   /* trigger next redraw */
}

/* ---------------------------------------------------------------------------
 * [rx thread] Frame ready callback: lightweight post, actual display in GUI thread
 * ------------------------------------------------------------------------- */
static void on_frame_ready(void)
{
	/* Post a "new frame" event to GUI server queue. If queue is full,
	 * gui_send_msg_to_server drops this one -- next frame will re-post.
	 * take_display always gets the latest, so no stale frame is shown. */
	gui_msg_t msg = {
		.event = GUI_EVENT_USER_DEFINE,
		.cb    = carplay_map_apply_cb,
	};
	gui_send_msg_to_server(&msg);
}

/* ---------------------------------------------------------------------------
 * Startup registration: runs once on GUI thread when gui_components_init starts
 * ------------------------------------------------------------------------- */
static int dashboard_img_display_init(void)
{
	dashboard_img_rx_register_notify(on_frame_ready);
	return 0;
}
GUI_INIT_APP_EXPORT(dashboard_img_display_init);
