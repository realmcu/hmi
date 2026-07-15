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
 * Dashboard image stream receiver (TCP server, 3-slot frame pool)
 *
 * Wire: JPG <size> <seq>\n + <size bytes JPEG>
 * Design: independent task, 3-slot pool with g_display/g_ready indices
 * ============================================================================ */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Must match Android NaviCaptureService.DEFAULT_TCP_PORT. */
#define DASHBOARD_IMG_RX_PORT       5004

/** Defensive JPEG size cap; buffers lazy-allocated per frame. */
#define DASHBOARD_IMG_RX_MAX_JPEG   (256 * 1024)

/** Frame-ready callback called on rx thread after a frame is received. */
typedef void (*dashboard_img_rx_notify_t)(void);

/**
 * @brief Image stream receiver task entry. Never returns.
 * @param param  unused
 */
void dash_board_img_rx_task(void *param);

/** Register frame-ready callback. Pass NULL to unregister. */
void dashboard_img_rx_register_notify(dashboard_img_rx_notify_t cb);

/**
 * @brief [GUI thread] Take latest ready frame for display.
 * @return buffer (gui_jpeg_file_head_t layout) or NULL if none
 */
const uint8_t *dashboard_img_rx_take_display(void);

/** [GUI thread] Release current display slot back to pool. */
void dashboard_img_rx_release_display(void);

/** @return total frames received since startup. */
uint32_t dashboard_img_rx_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_IMG_RX_H */
