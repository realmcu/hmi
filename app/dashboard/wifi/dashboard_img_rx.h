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

#ifndef DASHBOARD_IMG_RX_H
#define DASHBOARD_IMG_RX_H

/* ============================================================================
 * Dashboard image stream receiver
 *
 * Peer (Android app, see android/NaviJpgTcpSender.kt) encodes 400x480
 * navigation frames as JPEG and sends them over a persistent TCP connection.
 * This module runs a TCP **server**, receives frames into a 3-slot pool,
 * and wraps each frame as gui_jpeg_file_head_t for HoneyGUI consumption.
 *
 * Wire format (ASCII header + binary body, per frame):
 *     JPG <size> <seq>\n
 *     <size bytes of JPEG data>
 *
 * Design:
 *   - Independent task (dash_board_img_rx_task), not on wifi dispatcher.
 *   - **3-slot frame pool, two indices**: g_display (being decoded/painted)
 *     + g_ready (latest complete frame). Rx thread never picks the slot
 *     GUI is using, so the decoding frame is never released/overwritten.
 * ============================================================================ */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Listening port, must match Android side NaviCaptureService.DEFAULT_TCP_PORT. */
#define DASHBOARD_IMG_RX_PORT       5004

/** Per-frame JPEG size limit: **defensive cap** to reject abnormally large headers;
 *  buffers are lazy-allocated (malloc) per frame. 400x480 q60 is ~tens of KB. */
#define DASHBOARD_IMG_RX_MAX_JPEG   (256 * 1024)

/**
 * @brief Frame-ready callback type: called once on the **rx thread** after a frame is fully received.
 *
 * Callback should **not** do heavy work or block -- typical implementation just
 * posts a GUI event to the GUI thread (see dashboard_img_display.c). Events don't
 * need slot/sequence numbers; the GUI thread calls dashboard_img_rx_take_display()
 * to get the latest ready frame.
 */
typedef void (*dashboard_img_rx_notify_t)(void);

/**
 * @brief Image stream receiver task entry, called from app_example() in dashboard_main.c.
 *
 * Flow: create mutex -> wait for WiFi -> start TCP server (socket/bind/listen/accept)
 * -> parse frames, allocate buffers, build JPEG headers, publish to pool -> accept next.
 * Never returns.
 *
 * @param param  unused, pass NULL.
 */
void dash_board_img_rx_task(void *param);

/**
 * @brief Register frame-ready callback (see dashboard_img_rx_notify_t).
 *
 * Called once by the display glue module during GUI init (GUI_INIT_APP_EXPORT).
 * Pass NULL to unregister. When unregistered, reception continues but no
 * notification is sent.
 */
void dashboard_img_rx_register_notify(dashboard_img_rx_notify_t cb);

/**
 * @brief [GUI thread only] Take the latest ready frame for display. Returns buffer
 *        address directly usable with gui_img_set_src() (IMG_SRC_MEMADDR,
 *        gui_jpeg_file_head_t layout).
 *
 * Semantics (atomic under internal lock):
 *   1. Take current latest ready frame (g_ready); return NULL if none.
 *   2. Set it as display slot (g_display) -- rx thread will never write/release it.
 *   3. Previous display slot auto-returns to free for rx thread reuse.
 *
 * **Must only be called from GUI thread** (render and message processing are
 * serial, so transfer never races with draw).
 *
 * @retval !=NULL  latest ready frame buffer (gui_jpeg_file_head_t layout)
 * @retval NULL    no new frame to display
 */
const uint8_t *dashboard_img_rx_take_display(void);

/**
 * @brief [GUI thread only] Release current display slot, return to free pool.
 *
 * Call when display no longer needs received frames (e.g., carplay_map destroyed
 * or view switched out), to avoid permanent slot occupation. No-op if no display slot.
 */
void dashboard_img_rx_release_display(void);

/**
 * @brief Total number of fully received frames (for link verification).
 * @return Total frames successfully received since startup.
 */
uint32_t dashboard_img_rx_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_IMG_RX_H */
