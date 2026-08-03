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
 * Dashboard image stream receiver (TCP server, 3-slot frame pool)
 *
 * Independent task (not on wifi dispatcher) to avoid starving join/dhcp events.
 *
 * Frame pool (3 slots, 2 indices):
 *   g_display : slot being decoded/painted by GUI (-1=none)
 *   g_ready   : latest complete frame, not yet displayed (-1=none)
 *   Remaining = free/writable. Rx thread never touches g_display.
 *
 * Each frame: gui_jpeg_file_head_t header (16B) + raw JPEG.
 * ============================================================================ */

#include <stddef.h>     /* offsetof */
#include <stdio.h>      /* sscanf */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "section_config.h"

#include "lwip_netconf.h"
#include <lwip/sockets.h>

#include "def_file.h"
#include "draw_img.h"

#include "dashboard_wifi.h"
#include "dashboard_img_rx.h"

#define IMG_RX_LINE_MAX     64
#define IMG_RX_RECV_TIMEOUT_MS  15000

#define IMG_RX_SLOT_NUM     3
#define IMG_RX_HDR_OFFSET   ((uint32_t)offsetof(gui_jpeg_file_head_t, jpeg))

#define IMG_RX_DEF_W        400
#define IMG_RX_DEF_H        480

static uint8_t     *g_buf[IMG_RX_SLOT_NUM];
static uint32_t     g_cap[IMG_RX_SLOT_NUM];
static uint32_t     g_len[IMG_RX_SLOT_NUM];
static uint32_t     g_seq[IMG_RX_SLOT_NUM];

static int          g_display   = -1;
static int          g_ready     = -1;
static uint32_t     g_frame_cnt = 0;
static uint32_t     g_drop_cnt  = 0;

static rtos_mutex_t g_lock      = NULL;

static dashboard_img_rx_notify_t g_notify = NULL;
static dashboard_img_rx_state_notify_t g_state_notify = NULL;
static volatile dashboard_img_rx_state_t g_state = DASHBOARD_IMG_RX_STATE_INITIALIZING;
static volatile bool g_phone_connected = false;
static volatile bool g_streaming = false;

static void set_state(dashboard_img_rx_state_t state)
{
	if (g_state == state) {
		return;
	}
	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGRX] state %d -> %d\n", (int)g_state, (int)state);
	g_state = state;
	if (g_state_notify) {
		g_state_notify(state);
	}
}

static void update_connection_state(void)
{
	set_state(g_streaming ? DASHBOARD_IMG_RX_STATE_STREAMING :
			  (g_phone_connected ? DASHBOARD_IMG_RX_STATE_CONNECTED :
			   DASHBOARD_IMG_RX_STATE_WAITING));
}

/* ---------------------------------------------------------------------------
 * Socket helpers
 * ------------------------------------------------------------------------- */

/*
 * Byte-by-byte read until '\n' (not recv-bulk: header length is variable,
 * followed by binary JPEG, so bulk read would eat into JPEG bytes).
 */
static int recv_line(int fd, char *line, int cap)
{
	int n = 0;
	while (n < cap - 1) {
		char c;
		int r = recv(fd, &c, 1, 0);
		if (r <= 0) {
			if (r < 0 && n == 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				return -2;
			}
			RTK_LOGS(NOTAG, RTK_LOG_WARN,
					 "[IMGRX] recv_line end r=%d errno=%d after %d bytes\n", r, errno, n);
			return -1;
		}
		if (c == '\n') {
			line[n] = '\0';
			return n;
		}
		if (c == '\r') {
			continue;
		}
		line[n++] = c;
	}
	return -1;
}

/* Receive exactly want bytes */
static int recv_full(int fd, uint8_t *buf, uint32_t want)
{
	uint32_t got = 0;
	while (got < want) {
		int r = recv(fd, (char *)(buf + got), (int)(want - got), 0);
		if (r <= 0) {
			RTK_LOGS(NOTAG, RTK_LOG_WARN,
					 "[IMGRX] recv_full end r=%d errno=%d got=%u/%u\n",
					 r, errno, (unsigned int)got, (unsigned int)want);
			return -1;
		}
		got += (uint32_t)r;
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * Frame pool
 * ------------------------------------------------------------------------- */

/* Pick a writable slot (neither g_display nor g_ready). Under lock. */
static int claim_write_slot(void)
{
	rtos_mutex_take(g_lock, MUTEX_WAIT_TIMEOUT);

	int slot = -1;
	for (int i = 0; i < IMG_RX_SLOT_NUM; i++) {
		if (i != g_display && i != g_ready) {
			slot = i;
			break;
		}
	}

	rtos_mutex_give(g_lock);
	return slot;
}

/* Grow-to-fit: free+malloc on demand (old content overwritten, no realloc needed). */
static int ensure_capacity(int slot, uint32_t need)
{
	if (g_buf[slot] != NULL && g_cap[slot] >= need) {
		return 0;
	}
	if (g_buf[slot]) {
		rtos_heap_types_free(g_buf[slot]);
		g_buf[slot] = NULL;
		g_cap[slot] = 0;
	}
	g_buf[slot] = (uint8_t *)rtos_heap_types_malloc(need, TYPE_DRAM);
	if (g_buf[slot] == NULL) {
		RTK_LOGS(NOTAG, RTK_LOG_ERROR, "[IMGRX] alloc %u for slot%d failed\n",
				 (unsigned int)need, slot);
		return -1;
	}
	g_cap[slot] = need;
	return 0;
}

/* Parse JPEG dimensions from first SOF segment. Fallback to 400x480 on failure. */
static bool jpeg_get_dimensions(const uint8_t *d, uint32_t n, uint16_t *pw, uint16_t *ph)
{
	if (d == NULL || n < 4 || d[0] != 0xFF || d[1] != 0xD8) {
		return false;
	}

	uint32_t i = 2;
	while (i + 4 <= n) {
		if (d[i] != 0xFF) {
			i++;
			continue;
		}
		uint8_t m = d[i + 1];
		if (m == 0xFF) {
			i++;
			continue;
		}
		if (m == 0xD8 || m == 0xD9 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) {
			i += 2;
			continue;
		}
		uint32_t seglen = ((uint32_t)d[i + 2] << 8) | d[i + 3];
		if (seglen < 2) {
			return false;
		}
		if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) ||
			(m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
			if (i + 9 > n) {
				return false;
			}
			uint16_t h = ((uint16_t)d[i + 5] << 8) | d[i + 6];
			uint16_t w = ((uint16_t)d[i + 7] << 8) | d[i + 8];
			if (w == 0 || h == 0) {
				return false;
			}
			*pw = w;
			*ph = h;
			return true;
		}
		if (m == 0xDA) {
			return false;
		}
		i += 2 + seglen;
	}
	return false;
}

/* Write gui_jpeg_file_head_t header (16B) in slot prefix. */
static void build_jpeg_header(int slot, uint32_t size)
{
	gui_jpeg_file_head_t *wh = (gui_jpeg_file_head_t *)g_buf[slot];

	uint16_t w = IMG_RX_DEF_W, h = IMG_RX_DEF_H;
	jpeg_get_dimensions(g_buf[slot] + IMG_RX_HDR_OFFSET, size, &w, &h);

	memset(&wh->img_header, 0, sizeof(gui_rgb_data_head_t));
	wh->img_header.type = JPEG;
	wh->img_header.jpeg = 1;
	wh->img_header.w    = (short)w;
	wh->img_header.h    = (short)h;
	wh->size            = size;
	wh->dummy           = 0;
}

/* Publish: mark slot as latest ready frame (g_ready). */
static void publish_frame(int slot, uint32_t size, uint32_t seq)
{
	rtos_mutex_take(g_lock, MUTEX_WAIT_TIMEOUT);
	g_len[slot] = size;
	g_seq[slot] = seq;
	g_ready     = slot;
	g_frame_cnt++;
	rtos_mutex_give(g_lock);
	g_streaming = true;
	update_connection_state();

	if (g_notify) {
		g_notify();
	}
}

/* ---------------------------------------------------------------------------
 * Per-client frame loop
 * ------------------------------------------------------------------------- */
static void serve_client(int cfd)
{
	struct timeval tv;
	tv.tv_sec  = IMG_RX_RECV_TIMEOUT_MS / 1000;
	tv.tv_usec = (IMG_RX_RECV_TIMEOUT_MS % 1000) * 1000;
	setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

	const char *greeting = "READY\n";
	if (send(cfd, greeting, strlen(greeting), 0) < 0) {
		return;
	}

	for (;;) {
		char line[IMG_RX_LINE_MAX];
		int line_len = recv_line(cfd, line, sizeof(line));
		if (line_len == -2) {
			g_streaming = false;
			update_connection_state();
			continue;
		}
		if (line_len < 0) {
			return;
		}

		unsigned int size = 0, seq = 0;
		if (sscanf(line, "JPG %u %u", &size, &seq) != 2) {
			RTK_LOGS(NOTAG, RTK_LOG_WARN, "[IMGRX] bad header: %s\n", line);
			return;
		}

		if (size == 0 || size > DASHBOARD_IMG_RX_MAX_JPEG) {
			RTK_LOGS(NOTAG, RTK_LOG_WARN, "[IMGRX] size %u out of range\n", size);
			return;
		}

		int slot = claim_write_slot();
		if (slot < 0) {
			g_drop_cnt++;
			RTK_LOGS(NOTAG, RTK_LOG_WARN, "[IMGRX] no writable slot, drop frame\n");
			return;
		}

		if (ensure_capacity(slot, IMG_RX_HDR_OFFSET + size) < 0) {
			return;
		}

		if (recv_full(cfd, g_buf[slot] + IMG_RX_HDR_OFFSET, size) < 0) {
			return;
		}

		build_jpeg_header(slot, size);
		publish_frame(slot, size, seq);
	}
}

/* ---------------------------------------------------------------------------
 * Task entry
 * ------------------------------------------------------------------------- */
void dash_board_img_rx_task(void *param)
{
	(void)param;

	if (rtos_mutex_create(&g_lock) != RTK_SUCCESS) {
		RTK_LOGS(NOTAG, RTK_LOG_ERROR, "[IMGRX] mutex create failed\n");
		goto fail;
	}

	while (!dashboard_wifi_is_online()) {
		rtos_time_delay_ms(500);
	}
	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGRX] wifi online, start server :%d\n",
			 DASHBOARD_IMG_RX_PORT);

	for (;;) {
		int lfd = socket(AF_INET, SOCK_STREAM, 0);
		if (lfd < 0) {
			RTK_LOGS(NOTAG, RTK_LOG_ERROR, "[IMGRX] socket() failed\n");
			rtos_time_delay_ms(1000);
			continue;
		}

		int opt = 1;
		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family      = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port        = htons(DASHBOARD_IMG_RX_PORT);

		if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			RTK_LOGS(NOTAG, RTK_LOG_ERROR, "[IMGRX] bind() failed\n");
			closesocket(lfd);
			rtos_time_delay_ms(1000);
			continue;
		}

		if (listen(lfd, 1) < 0) {
			RTK_LOGS(NOTAG, RTK_LOG_ERROR, "[IMGRX] listen() failed\n");
			closesocket(lfd);
			rtos_time_delay_ms(1000);
			continue;
		}
		update_connection_state();

		for (;;) {
			struct sockaddr_in cli;
			socklen_t clilen = sizeof(cli);
			int cfd = accept(lfd, (struct sockaddr *)&cli, &clilen);
			if (cfd < 0) {
				break;
			}

			serve_client(cfd);

			closesocket(cfd);
			g_streaming = false;
			update_connection_state();
		}

		closesocket(lfd);
		rtos_time_delay_ms(1000);
	}

fail:
	for (int i = 0; i < IMG_RX_SLOT_NUM; i++) {
		if (g_buf[i]) {
			rtos_heap_types_free(g_buf[i]);
			g_buf[i] = NULL;
		}
	}
	rtos_task_delete(NULL);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */
void dashboard_img_rx_register_notify(dashboard_img_rx_notify_t cb)
{
	g_notify = cb;
}

void dashboard_img_rx_register_state_notify(dashboard_img_rx_state_notify_t cb)
{
	g_state_notify = cb;
	if (cb) {
		cb(g_state);
	}
}

dashboard_img_rx_state_t dashboard_img_rx_get_state(void)
{
	return g_state;
}

void dashboard_img_rx_set_phone_connected(bool connected)
{
	g_phone_connected = connected;
	if (!connected) {
		g_streaming = false;
	}
	update_connection_state();
}

const uint8_t *dashboard_img_rx_take_display(void)
{
	if (g_lock == NULL) {
		return NULL;
	}

	rtos_mutex_take(g_lock, MUTEX_WAIT_TIMEOUT);

	if (g_ready < 0) {
		rtos_mutex_give(g_lock);
		return NULL;
	}

	int target  = g_ready;
	g_display   = target;
	g_ready     = -1;

	const uint8_t *buf = g_buf[target];
	rtos_mutex_give(g_lock);
	return buf;
}

void dashboard_img_rx_release_display(void)
{
	if (g_lock == NULL) {
		return;
	}
	rtos_mutex_take(g_lock, MUTEX_WAIT_TIMEOUT);
	g_display = -1;
	rtos_mutex_give(g_lock);
}

uint32_t dashboard_img_rx_frame_count(void)
{
	return g_frame_cnt;
}

/* ---------------------------------------------------------------------------
 * Shell command "img_rx": print frame pool status
 * ------------------------------------------------------------------------- */
static u32 cmd_dashboard_img_rx(u16 argc, u8 *argv[])
{
	(void)argc;
	(void)argv;

	if (g_lock == NULL) {
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGRX] not initialized yet\n");
		return TRUE;
	}

	rtos_mutex_take(g_lock, MUTEX_WAIT_TIMEOUT);
	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
			 "[IMGRX] frames=%u drops=%u display=%d ready=%d\n",
			 (unsigned int)g_frame_cnt, (unsigned int)g_drop_cnt, g_display, g_ready);
	for (int i = 0; i < IMG_RX_SLOT_NUM; i++) {
		const char *role = (i == g_display) ? "DISP" :
						   (i == g_ready)   ? "RDY " : "free";
		RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
				 "[IMGRX]   slot%d %s seq=%u size=%u cap=%u\n",
				 i, role, (unsigned int)g_seq[i],
				 (unsigned int)g_len[i], (unsigned int)g_cap[i]);
	}
	rtos_mutex_give(g_lock);
	return TRUE;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE dashboard_img_rx_cmd_table[] = {
	{"img_rx", cmd_dashboard_img_rx},
};
