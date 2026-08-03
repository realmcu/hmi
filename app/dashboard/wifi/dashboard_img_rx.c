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
 * Dashboard image stream receiver (TCP server -> stream transport)
 *
 * Independent task (not on wifi dispatcher) to avoid starving join/dhcp events.
 *
 * The receiver is the single producer. It receives each JPEG directly into a
 * transport-owned buffer and commits it for the gui_stream consumer.
 * ============================================================================ */

#include <stdio.h>      /* sscanf */
#include <string.h>
#include <errno.h>

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "section_config.h"

#include "lwip_netconf.h"
#include <lwip/sockets.h>

#include "stream_transport.h"

#include "dashboard_wifi.h"
#include "dashboard_img_rx.h"

#define IMG_RX_LINE_MAX     64
#define IMG_RX_RECV_TIMEOUT_MS  15000

static uint32_t     g_frame_cnt = 0;
static uint32_t     g_drop_cnt  = 0;
static dashboard_img_rx_state_notify_t g_state_notify = NULL;
static volatile dashboard_img_rx_state_t g_state = DASHBOARD_IMG_RX_STATE_INITIALIZING;
static volatile bool g_phone_connected = false;
static volatile bool g_streaming = false;

extern stp_transport_t *gui_stream_transport_get(void);

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

static int recv_discard(int fd, uint32_t want)
{
	uint8_t scratch[512];
	while (want > 0) {
		uint32_t chunk = want < sizeof(scratch) ? want : sizeof(scratch);
		if (recv_full(fd, scratch, chunk) < 0) {
			return -1;
		}
		want -= chunk;
	}
	return 0;
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

		stp_transport_t *transport = gui_stream_transport_get();
		stp_frame_t frame;
		if (transport == NULL || !stp_acquire_free(transport, (uint32_t)size, &frame)) {
			g_drop_cnt++;
			if (recv_discard(cfd, (uint32_t)size) < 0) {
				return;
			}
			continue;
		}

		if (recv_full(cfd, (uint8_t *)frame.addr, (uint32_t)size) < 0) {
			stp_release(transport, &frame);
			return;
		}

		if (!stp_commit(transport, &frame, (uint32_t)size, true)) {
			g_drop_cnt++;
			continue;
		}

		(void)seq;
		g_frame_cnt++;
		g_streaming = true;
		update_connection_state();
	}
}

/* ---------------------------------------------------------------------------
 * Task entry
 * ------------------------------------------------------------------------- */
void dash_board_img_rx_task(void *param)
{
	(void)param;

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

}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */
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

	RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
			 "[IMGRX] frames=%u drops=%u state=%d\n",
			 (unsigned int)g_frame_cnt, (unsigned int)g_drop_cnt, (int)g_state);
	stp_transport_t *transport = gui_stream_transport_get();
	if (transport != NULL) {
		stp_dump_usage(transport);
	}
	return TRUE;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE dashboard_img_rx_cmd_table[] = {
	{"img_rx", cmd_dashboard_img_rx},
};
