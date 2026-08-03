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
#include "gui_qbcode.h"

#include "ameba_soc.h"
#include "dashboard_wifi.h"
#include "dashboard_img_rx.h"

extern void gui_fb_change(void);

/* carplay_map: created by DashboardMain_ui.c, NULL when view switched out. */
extern gui_img_t *carplay_map;

#define STRINGIFY_VALUE_(value) #value
#define STRINGIFY_VALUE(value)  STRINGIFY_VALUE_(value)

#define HONEYBOX_QR_URL \
	"https://github.com/realmcu/HoneyBox/releases/download/v0.8.7/HoneyBox.apk" \
	"?modelid=RTL8782" \
	"&sn=0001" \
	"&action=513&ssid=" DASHBOARD_AP_SSID \
	"&pwd=" DASHBOARD_AP_PASSWORD \
	"&ip=" DASHBOARD_AP_IP \
	"&port=" STRINGIFY_VALUE(DASHBOARD_IMG_RX_PORT)

static gui_qbcode_t *g_carplay_qr = NULL;

static void carplay_display_apply_state(dashboard_img_rx_state_t state)
{
	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGDISPLAY] apply state=%d map=%p\n",
			 (int)state, carplay_map);
	if (carplay_map == NULL) {
		g_carplay_qr = NULL;
		return;
	}

	bool show_qr = (state == DASHBOARD_IMG_RX_STATE_INITIALIZING ||
				state == DASHBOARD_IMG_RX_STATE_WAITING);
	gui_obj_t *parent = GUI_BASE(carplay_map)->parent;
	if (show_qr && g_carplay_qr == NULL) {
		gui_obj_hidden(GUI_BASE(carplay_map), true);
		gui_fb_change();
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGDISPLAY] encoding QR len=%u\n",
				 (unsigned int)(sizeof(HONEYBOX_QR_URL) - 1));
		g_carplay_qr = gui_qbcode_create(parent, "carplay_connect_qr", 393, 77, 370, 370,
								 QRCODE_DISPLAY_SECTION, QRCODE_ENCODE_TEXT);
		gui_qbcode_config(g_carplay_qr, (uint8_t *)HONEYBOX_QR_URL,
						   (uint32_t)(sizeof(HONEYBOX_QR_URL) - 1), 3);
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
				 "[IMGDISPLAY] QR created data=%p len=%u\n", g_carplay_qr->data,
				 (unsigned int)(sizeof(HONEYBOX_QR_URL) - 1));
	}

	gui_obj_hidden(GUI_BASE(carplay_map), show_qr);
	if (g_carplay_qr != NULL) {
		gui_obj_hidden(GUI_BASE(g_carplay_qr), !show_qr);
	}
	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
			 "[IMGDISPLAY] state=%d show_qr=%d map_hidden=%d qr=%p qr_hidden=%d\n",
			 (int)state, (int)show_qr, (int)GUI_BASE(carplay_map)->hidden,
			 g_carplay_qr, g_carplay_qr ? (int)GUI_BASE(g_carplay_qr)->hidden : -1);

	if (state == DASHBOARD_IMG_RX_STATE_CONNECTED) {
		dashboard_img_rx_release_display();
		gui_img_set_src(carplay_map, (const uint8_t *)"/resource/carplay/carplay_map_00.bin",
						IMG_SRC_FILESYS);
		gui_img_refresh_size(carplay_map);
	}
	gui_fb_change();
}

static void carplay_state_apply_cb(void *param)
{
	(void)param;
	carplay_display_apply_state(dashboard_img_rx_get_state());
}

static void on_state_changed(dashboard_img_rx_state_t state)
{
	gui_msg_t msg = {
		.event = GUI_EVENT_USER_DEFINE,
		.cb    = carplay_state_apply_cb,
	};
	(void)state;
	gui_send_msg_to_server(&msg);
}

void dashboard_img_display_view_ready(void)
{
	g_carplay_qr = NULL;
	carplay_display_apply_state(dashboard_img_rx_get_state());
}

void dashboard_img_display_view_released(void)
{
	g_carplay_qr = NULL;
}

static void carplay_map_apply_cb(void *param)
{
	(void)param;

	if (carplay_map == NULL) {
		return;
	}

	const uint8_t *buf = dashboard_img_rx_take_display();
	if (buf == NULL) {
		return;
	}

	gui_img_set_src(carplay_map, buf, IMG_SRC_MEMADDR);
	gui_img_refresh_size(carplay_map);
	gui_obj_hidden(GUI_BASE(carplay_map), false);
	if (g_carplay_qr != NULL) {
		gui_obj_hidden(GUI_BASE(g_carplay_qr), true);
	}
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
	dashboard_img_rx_register_state_notify(on_state_changed);
	return 0;
}
GUI_INIT_APP_EXPORT(dashboard_img_display_init);
