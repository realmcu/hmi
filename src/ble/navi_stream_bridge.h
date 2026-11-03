/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _NAVI_STREAM_BRIDGE_H_
#define _NAVI_STREAM_BRIDGE_H_

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Route validated projection frames into the HoneyGUI stream widget.
 *
 * Installs the GUI-side frame callback of the navigation projection service.
 * The callback runs on the GUI server thread and copies each frame into the
 * live-video transport that gui_stream reads from (see
 * ui_design/src/user/DashboardMain_user.c), so the BLE receive slots can be
 * recycled immediately after the callback returns.
 *
 * Call after app_ble_service_init(): that is where the service is registered.
 */
void navi_stream_bridge_init(void);

/**
 * @brief Reset the per-session state of the bridge.
 *
 * Call when a link is dropped, together with navi_projection_close(). The next
 * session then switches to the projection view again on its first frame.
 */
void navi_stream_bridge_on_disconnect(void);

#ifdef __cplusplus
}
#endif
#endif
