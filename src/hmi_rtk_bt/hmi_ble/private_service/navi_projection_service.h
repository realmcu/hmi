/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _NAVI_PROJECTION_SERVICE_H_
#define _NAVI_PROJECTION_SERVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app_msg.h"
#include "profile_server_ext.h"

#define NAVI_PROTOCOL_VERSION              0x02
#define NAVI_FEATURE_RAW_ATT_PACKET        0x01
#define NAVI_FEATURE_FRAME_CRC32           0x02
#define NAVI_REQUIRED_FEATURES             0x03

#define NAVI_FRAME_SLOTS                   3U
#define NAVI_MAX_JPEG_SIZE                 (128U * 1024U)
#define NAVI_GUI_JPEG_HEADER_SIZE          16U

#define NAVI_QR_URL_MAX_LEN                256U
#define NAVI_QR_URL_BASE                   \
    "https://github.com/realmcu/HoneyBox/releases/latest/download/HoneyBox.apk"
#define NAVI_QR_MODEL_ID                   "RTL8773E"
#define NAVI_QR_SN                         "0529"

typedef enum
{
    NAVI_STATE_IDLE = 0,
    NAVI_STATE_VALIDATING,
    NAVI_STATE_ACTIVE
} navi_state_t;

/** Deprecated: frames are now published to the stream transport, not decoded
 *  through this callback. Kept for source compatibility; never invoked. */
typedef bool (*navi_gui_frame_cb_t)(const uint8_t *p_gui_src,
                                    uint32_t jpeg_len,
                                    uint16_t frame_seq);

/** Called when a session reset requires the GUI to restore the QR page. */
typedef void (*navi_gui_reset_cb_t)(void);

/** Called after the controller has finalized its public address. */
typedef void (*navi_qr_ready_cb_t)(void);

/** Called on the GUI task once per session when the first frame has been
 *  published to the transport, so the QR page can be hidden. */
typedef void (*navi_gui_stream_active_cb_t)(void);

/** Outbound L1 frame sink. Installed by the L0 carrier that owns the BLE link
 *  (HoneyProtocol 2.2: the HMIS private service, notify 0xFFC2). */
typedef void (*navi_carrier_tx_t)(const uint8_t *data, uint16_t len);

/** Initialise the session engine without registering any GATT service of its
 *  own: L1/L2 ride on the HMIS private service, so only the gap callback and
 *  the poll timer are needed here (QR helpers and stream transport hooks are
 *  installed by the UI layer as before). */
void navi_projection_core_init(void);

/** Bind / unbind the outbound L1 carrier. */
void navi_projection_set_carrier(navi_carrier_tx_t tx);

/** Feed raw bytes received from the carrier into the L1 reassembler. */
void navi_projection_feed_l1(const uint8_t *data, uint16_t len);

void navi_projection_set_gui_callbacks(navi_gui_frame_cb_t frame_cb,
                                       navi_gui_reset_cb_t reset_cb,
                                       navi_qr_ready_cb_t qr_ready_cb);
void navi_projection_gui_attached(void);
void navi_projection_gui_detached(void);

/**
 * @brief Bind the single-producer/single-consumer stream transport that
 *        completed JPEG frames are published into.
 *
 * The handle is an ::stp_transport_t (declared void here to keep this BLE
 * header free of the GUI-widget dependency). Received frames are reassembled
 * directly into transport buffers (zero-copy) and committed on completion;
 * the gui_stream widget consumes and decodes them on the GUI task. Pass NULL
 * to detach. Must be called before streaming starts.
 */
void navi_projection_set_transport(void *transport);

/** Register the GUI-task callback fired once when streaming becomes active. */
void navi_projection_set_stream_active_cb(navi_gui_stream_active_cb_t active_cb);

/** BT-task hooks used by the existing GAP/task integration. */
void navi_projection_handle_gap_msg(T_IO_MSG *p_gap_msg);
void navi_projection_process(void);
void navi_projection_on_address_ready(const uint8_t address[6]);

navi_state_t navi_projection_get_state(void);
T_SERVER_ID navi_projection_get_service_id(void);

/** Validate an 8-bit baseline JPEG and require SOF dimensions to match OPEN. */
bool navi_projection_validate_jpeg(const uint8_t *jpeg, uint32_t len,
                                   uint16_t expected_w, uint16_t expected_h);

/** Build the exact HoneyBox URL using the finalized public address in [5]..[0] order. */
int navi_projection_build_qr_url(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _NAVI_PROJECTION_SERVICE_H_ */
