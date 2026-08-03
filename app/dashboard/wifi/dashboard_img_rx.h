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
 * Dashboard image stream receiver (TCP server -> stream transport)
 *
 * Wire: JPG <size> <seq>\n + <size bytes JPEG>
 * Design: independent task, one complete JPEG per stream-transport buffer
 * ============================================================================ */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Must match Android NaviCaptureService.DEFAULT_TCP_PORT. */
#define DASHBOARD_IMG_RX_PORT       5004

/** Defensive JPEG size cap; must match the stream transport buffer size. */
#define DASHBOARD_IMG_RX_MAX_JPEG   (256 * 1024)

typedef enum {
	DASHBOARD_IMG_RX_STATE_INITIALIZING,
	DASHBOARD_IMG_RX_STATE_WAITING,
	DASHBOARD_IMG_RX_STATE_CONNECTED,
	DASHBOARD_IMG_RX_STATE_STREAMING,
} dashboard_img_rx_state_t;

/** Connection/stream state callback, invoked from the image receiver task. */
typedef void (*dashboard_img_rx_state_notify_t)(dashboard_img_rx_state_t state);

/**
 * @brief Image stream receiver task entry. Never returns.
 * @param param  unused
 */
void dash_board_img_rx_task(void *param);

/** Register state callback. The current state is delivered immediately. */
void dashboard_img_rx_register_state_notify(dashboard_img_rx_state_notify_t cb);

/** @return current phone connection/stream state. */
dashboard_img_rx_state_t dashboard_img_rx_get_state(void);

/** Update whether a phone is associated with the dashboard SoftAP. */
void dashboard_img_rx_set_phone_connected(bool connected);

/** @return total frames received since startup. */
uint32_t dashboard_img_rx_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_IMG_RX_H */
