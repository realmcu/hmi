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
 * Dashboard image stream receiver implementation
 *
 * Role: TCP **server** (Android app connects actively, see
 * android/NaviJpgTcpSender.kt / NaviCaptureService.kt).
 *
 * Why an independent task instead of riding on the dashboard_wifi dispatcher:
 *   OTA is a "block-30s-then-done" task; image receiving is a **permanently**
 *   blocking accept/recv loop that would starve WiFi join/dhcp events.
 *   So this module has its own persistent task (like dashboard_ble).
 *   It only uses dashboard_wifi_is_online() to check network readiness.
 *
 * -------------------------- Frame pool (3 slots, 2 indices) --------------------------
 * 3 slots, two indices describe all state (under g_lock):
 *
 *     g_display : slot pointed to by carplay_map, being decoded/painted by GUI (-1=none)
 *     g_ready   : latest complete frame, not yet displayed (-1=none)
 *
 *   Remaining slots (neither g_display nor g_ready) are "free/writable".
 *   3 slots cover the worst case: 1 displaying + 1 ready + 1 receiving.
 *
 *   - rx thread: claim_write_slot() picks the slot that is neither g_display nor
 *     g_ready. It **never** touches g_display, ensuring the frame being decoded/
 *     painted is never released or overwritten.
 *   - GUI thread: take_display() sets g_ready as g_display and clears g_ready.
 *     Previous g_display auto-returns to free.
 *     Rendering and message processing are serial on the GUI thread.
 *
 *   Safety invariant: the slot w being written by the rx thread is never
 *   g_display or g_ready, so the receiving buffer never overlaps with the
 *   display buffer.
 *
 * Each frame is in-place formatted as gui_jpeg_file_head_t: first 16 bytes
 * hold header (type=JPEG + dimensions + size), raw JPEG follows at offset 16.
 * The take_display return value can be fed directly to gui_img_set_src(MEMADDR).
 * ============================================================================ */

#include <stddef.h>     /* offsetof */
#include <stdio.h>      /* sscanf */
#include <stdlib.h>
#include <string.h>
#include <errno.h>      /* errno (LWIP_ERRNO_STDINCLUDE is set in lwipopts, socket layer uses this) */

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "section_config.h"

/* Defensive double-check for online status. */
#include "lwip_netconf.h"
#include <lwip/sockets.h>

/* HoneyGUI image data format (gui_jpeg_file_head_t / gui_rgb_data_head_t / JPEG enum).
 * This file only depends on the "data format" header, not widget/server.
 * Those are in dashboard_img_display.c, decoupled via dashboard_img_rx_notify_t. */
#include "def_file.h"
#include "draw_img.h"

#include "dashboard_wifi.h"
#include "dashboard_img_rx.h"

#define LOG_TAG "DASHBOARD-IMGRX"

/* recv_line "JPG <size> <seq>\n" 64
 * '\n' */
#define IMG_RX_LINE_MAX     64

/* Per-client receive timeout (ms). Peer is 5fps (~200ms between frames), WiFi power save
 * may cause second-level pauses; 15s is generous. Real dead connections are reclaimed within 15s. */
#define IMG_RX_RECV_TIMEOUT_MS  15000

/* ---------------------------------------------------------------------------
 * Frame pool (3 slots, 2 indices)
 * ------------------------------------------------------------------------- */
/* Fixed at 3 slots: 1 displaying + 1 ready + 1 receiving, exactly enough.
 * claim always gets a slot that is neither g_display nor g_ready. */
#define IMG_RX_SLOT_NUM     3

/* JPEG raw stream offset in slot buffer: fixed prefix before jpeg[] in gui_jpeg_file_head_t
 * (gui_rgb_data_head_t 8B + size 4B + dummy 4B = 16B). Use offsetof to avoid manual calc. */
#define IMG_RX_HDR_OFFSET   ((uint32_t)offsetof(gui_jpeg_file_head_t, jpeg))

/* Fallback dimensions when SOF parsing fails (peer always sends 400x480 nav frame). */
#define IMG_RX_DEF_W        400
#define IMG_RX_DEF_H        480

/* Slot buffers are **lazy-allocated** (malloc), not pre-allocated at boot. Grow-to-fit:
 * free+malloc only when a larger frame arrives. 400x480 q60 is ~tens of KB.
 * Note: g_cap records total capacity including the 16B header prefix. */
static uint8_t     *g_buf[IMG_RX_SLOT_NUM];             /* +JPEG */
static uint32_t     g_cap[IMG_RX_SLOT_NUM];
static uint32_t     g_len[IMG_RX_SLOT_NUM];            /* JPEG */
static uint32_t     g_seq[IMG_RX_SLOT_NUM];            /* / */

static int          g_display   = -1;                  /* carplay_map -1= */
static int          g_ready     = -1;                  /* -1= */
static uint32_t     g_frame_cnt = 0;
static uint32_t     g_drop_cnt  = 0;                   /* 3 0 */

static rtos_mutex_t g_lock      = NULL;                /* g_display / g_ready */

static dashboard_img_rx_notify_t g_notify = NULL;

/* ===========================================================================
 * socket
 * =========================================================================== */

/**
 * @brief Read one byte at a time until '\n', return the line (without trailing newline, '\r' stripped).
 *
 * Why byte-by-byte instead of recv in bulk: header length is variable and followed
 * immediately by binary JPEG data. Reading too much would eat into JPEG bytes.
 * Byte-by-byte is fine since there is only one header line per frame.
 *
 * @param fd    connected socket
 * @param line  output buffer
 * @param cap   line buffer capacity (incl. trailing '\0')
 * @retval >=0  line length (excl. '\0')
 * @retval -1   connection closed / error / line too long
 */
static int recv_line(int fd, char *line, int cap)
{
	int n = 0;
	while (n < cap - 1) {
		char c;
		int r = recv(fd, &c, 1, 0);
		if (r <= 0) {
			/* r==0(FIN)r<0/(errno=11 EWOULDBLOCK RCVTIMEO) */
			RTK_LOGS(NOTAG, RTK_LOG_WARN,
					 "[IMGRX] recv_line end r=%d errno=%d after %d bytes\n", r, errno, n);
			return -1;
		}
		if (c == '\n') {
			line[n] = '\0';
			return n;
		}
		if (c == '\r') {
			continue;                  /* CRLF */
		}
		line[n++] = c;
	}
	return -1;
}

/**
 * @brief Receive exactly want bytes from socket into buf.
 * @retval 0   success
 * @retval -1  connection closed / error
 */
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

/**
 * @brief [rx thread] Pick a writable slot (neither g_display nor g_ready), return slot index.
 *
 * Reads g_display/g_ready under lock. With 3 slots total, at most 2 are taken,
 * so at least 1 is writable. **Never** selects g_display.
 *
 * No need to mark the selected slot: rx is single-writer and serial, won't
 * claim again until publish. GUI thread only touches g_display/g_ready.
 *
 * @retval >=0  writable slot index
 * @retval -1   should not happen (SLOT_NUM>=3 guarantees a free slot)
 */
static int claim_write_slot(void)
{
	rtos_mutex_take(g_lock, MUTEX_WAIT_TIMEOUT);

	int slot = -1;
	for (int i = 0; i < IMG_RX_SLOT_NUM; i++) {
		if (i != g_display && i != g_ready) {
			slot = i;                  /* → */
			break;
		}
	}

	rtos_mutex_give(g_lock);
	return slot;
}

/**
 * @brief [rx thread] Ensure writable slot has at least need bytes of capacity.
 *
 * Only called on a claimed writable slot (rx-exclusive, GUI only touches
 * g_display slot), so modifying g_buf[slot] pointer needs **no lock**.
 * Grow-to-fit: only increases; uses free+malloc instead of realloc since
 * old content is immediately overwritten.
 *
 * @retval 0   ready
 * @retval -1  allocation failed (slot remains free, caller disconnects)
 */
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

/**
 * @brief Parse real dimensions from JPEG stream (read first SOF segment).
 *
 * gui_img uses w/h from header as draw area dimensions (draw_img_load_scale
 * reads head.w/h directly), so **real** dimensions must be provided. On
 * parse failure returns false, caller falls back to 400x480.
 */
static bool jpeg_get_dimensions(const uint8_t *d, uint32_t n, uint16_t *pw, uint16_t *ph)
{
	if (d == NULL || n < 4 || d[0] != 0xFF || d[1] != 0xD8) {
		return false;                  /* JPEG SOI */
	}

	uint32_t i = 2;
	while (i + 4 <= n) {
		if (d[i] != 0xFF) {            /* 0xFF */
			i++;
			continue;
		}
		uint8_t m = d[i + 1];
		if (m == 0xFF) {               /* 0xFF... */
			i++;
			continue;
		}
		/* SOI/EOI/RSTn/TEM 2 */
		if (m == 0xD8 || m == 0xD9 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) {
			i += 2;
			continue;
		}
		uint32_t seglen = ((uint32_t)d[i + 2] << 8) | d[i + 3];
		if (seglen < 2) {
			return false;
		}
		/* SOF0..SOF15 DHT(C4)/JPG(C8)/DAC(CC) */
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
		if (m == 0xDA) {               /* SOS SOF */
			return false;
		}
		i += 2 + seglen;
	}
	return false;
}

/**
 * @brief [rx thread] Write gui_jpeg_file_head_t header in-place at slot buffer prefix.
 *
 * JPEG stream is already at g_buf[slot] + IMG_RX_HDR_OFFSET; this only fills
 * the first 16 header bytes, making the buffer a valid IMG_SRC_MEMADDR/JPEG source.
 */
static void build_jpeg_header(int slot, uint32_t size)
{
	gui_jpeg_file_head_t *wh = (gui_jpeg_file_head_t *)g_buf[slot];

	uint16_t w = IMG_RX_DEF_W, h = IMG_RX_DEF_H;
	jpeg_get_dimensions(g_buf[slot] + IMG_RX_HDR_OFFSET, size, &w, &h);

	memset(&wh->img_header, 0, sizeof(gui_rgb_data_head_t));
	wh->img_header.type = JPEG;        /* draw_img_cache head->type==JPEG */
	wh->img_header.jpeg = 1;
	wh->img_header.w    = (short)w;
	wh->img_header.h    = (short)h;
	wh->size            = size;        /* gui_acc_jpeg_load */
	wh->dummy           = 0;
}

/* ===========================================================================
 * slot ""g_ready
 * =========================================================================== */
static void publish_frame(int slot, uint32_t size, uint32_t seq)
{
	rtos_mutex_take(g_lock, MUTEX_WAIT_TIMEOUT);
	g_len[slot] = size;
	g_seq[slot] = seq;
	g_ready     = slot;                /* g_ready */
	g_frame_cnt++;
	rtos_mutex_give(g_lock);

	/* post */
	if (g_notify) {
		g_notify();
	}
}

/* ===========================================================================
 *
 * =========================================================================== */
static void serve_client(int cfd)
{
	struct timeval tv;
	tv.tv_sec  = IMG_RX_RECV_TIMEOUT_MS / 1000;
	tv.tv_usec = (IMG_RX_RECV_TIMEOUT_MS % 1000) * 1000;
	setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

	/* greeting NaviJpgTcpSender.readReadyGreeting */
	const char *greeting = "READY\n";
	if (send(cfd, greeting, strlen(greeting), 0) < 0) {
		return;
	}

	for (;;) {
		char line[IMG_RX_LINE_MAX];
		if (recv_line(cfd, line, sizeof(line)) < 0) {
			return;                    /* → accept */
		}
		RTK_LOGS(NOTAG, RTK_LOG_INFO, "[IMGRX] rx header: '%s'\n", line);

		unsigned int size = 0, seq = 0;
		if (sscanf(line, "JPG %u %u", &size, &seq) != 2) {
			RTK_LOGS(NOTAG, RTK_LOG_WARN, "[IMGRX] bad header: %s\n", line);
			return;
		}

		if (size == 0 || size > DASHBOARD_IMG_RX_MAX_JPEG) {
			RTK_LOGS(NOTAG, RTK_LOG_WARN, "[IMGRX] size %u out of range\n", size);
			return;
		}

		/* g_display g_ready GUI */
		int slot = claim_write_slot();
		if (slot < 0) {
			/* SLOT_NUM>=3 */
			g_drop_cnt++;
			RTK_LOGS(NOTAG, RTK_LOG_WARN, "[IMGRX] no writable slot, drop frame\n");
			return;
		}

		/* + JPEG 16B
		 * / publish g_display/g_ready
		 * */
		if (ensure_capacity(slot, IMG_RX_HDR_OFFSET + size) < 0) {
			return;
		}

		/* JPEG buf+16GUI */
		if (recv_full(cfd, g_buf[slot] + IMG_RX_HDR_OFFSET, size) < 0) {
			return;
		}

		/* gui_jpeg_file_head_t + */
		build_jpeg_header(slot, size);
		publish_frame(slot, size, seq);

		RTK_LOGS(NOTAG, RTK_LOG_INFO,
				 "[IMGRX] frame #%u seq=%u size=%u -> slot%d\n",
				 (unsigned int)g_frame_cnt, seq, size, slot);
	}
}

/* ===========================================================================
 *
 * =========================================================================== */
void dash_board_img_rx_task(void *param)
{
	(void)param;

	/* 1) ensure_capacity
	 * */
	if (rtos_mutex_create(&g_lock) != RTK_SUCCESS) {
		RTK_LOGS(NOTAG, RTK_LOG_ERROR, "[IMGRX] mutex create failed\n");
		goto fail;
	}

	/* 2) WiFi WiFishell/ */
	while (!dashboard_wifi_is_online()) {
		rtos_time_delay_ms(500);
	}
	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGRX] wifi online, start server :%d\n",
			 DASHBOARD_IMG_RX_PORT);

	/* 3) TCP socket accept serve_client */
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

		for (;;) {
			struct sockaddr_in cli;
			socklen_t clilen = sizeof(cli);
			int cfd = accept(lfd, (struct sockaddr *)&cli, &clilen);
			if (cfd < 0) {
				/* accept socket */
				break;
			}
			RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGRX] client connected\n");

			serve_client(cfd);

			closesocket(cfd);
			RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "[IMGRX] client disconnected\n");
			/* acceptNaviJpgTcpSender */
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

/* ===========================================================================
 * API +
 * =========================================================================== */
void dashboard_img_rx_register_notify(dashboard_img_rx_notify_t cb)
{
	g_notify = cb;                     /* GUI */
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

	/*
	 * 1. target = g_ready
	 * 2. g_display = target
	 * 3. g_display */
	int target  = g_ready;
	g_display   = target;
	g_ready     = -1;

	const uint8_t *buf = g_buf[target];
	rtos_mutex_give(g_lock);
	return buf;                        /* gui_jpeg_file_head_t gui_img_set_src */
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

/* ===========================================================================
 * shell "img_rx" +
 * =========================================================================== */
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
