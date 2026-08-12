/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _NAVI_PROJECTION_H_
#define _NAVI_PROJECTION_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include "stddef.h"
#include "stdint.h"
#include "stdbool.h"

/* GATT service and characteristic UUIDs. */
#define NAVI_SERVICE_UUID      0xFFD0

#define NAVI_CHAR_CTRL_TX_UUID 0xFFD1 /* Write: app to device. */
#define NAVI_CHAR_CTRL_RX_UUID 0xFFD2 /* Notify: device to app. */
#define NAVI_CHAR_DATA_TX_UUID 0xFFD3 /* Write Command: app to device. */
#define NAVI_CHAR_DATA_RX_UUID 0xFFD4 /* Notify: device to app. */

/* L1 control-channel framing. */
#define NAVI_L1_SYNC        0xAB
#define NAVI_L1_HEADER_LEN  8 /* sync + control + length + CRC16 + sequence */

#define NAVI_L1_CTRL_DATA   0x00
#define NAVI_L1_CTRL_ACK    0x10
#define NAVI_L1_CTRL_ERROR  0x30

#define NAVI_L1_MAX_PAYLOAD 248

/* L2 protocol command and keys. */
#define NAVI_L2_CMD     0x11

#define NAVI_KEY_OPEN   0x01
#define NAVI_KEY_ACK    0x02
#define NAVI_KEY_CLOSE  0x03
#define NAVI_KEY_ERROR  0x04
#define NAVI_KEY_FRAME  0x05
#define NAVI_KEY_CREDIT 0x06
#define NAVI_KEY_REPORT 0x07

/* command(1) + reserved(1) + key(1) + value length(2) */
#define NAVI_L2_HEADER_LEN 5

/* OPEN/ACK result codes. */
#define NAVI_RESULT_OK            0x00
#define NAVI_RESULT_BUSY          0x01
#define NAVI_RESULT_UNSUPPORTED   0x02
#define NAVI_RESULT_DECODER_ERROR 0x03
#define NAVI_RESULT_LOW_MEMORY    0x04

/* CLOSE reason codes. */
#define NAVI_CLOSE_NORMAL         0x00
#define NAVI_CLOSE_USER_CANCEL    0x01
#define NAVI_CLOSE_CREDIT_TIMEOUT 0x02
#define NAVI_CLOSE_ENCODE_ERROR   0x03

/* ERROR codes. */
#define NAVI_ERR_BUFFER_OVERFLOW  0x01
#define NAVI_ERR_DECODE_FAILED    0x02
#define NAVI_ERR_TIMEOUT          0x03
#define NAVI_ERR_UNEXPECTED_FRAME 0x04
#define NAVI_ERR_CRC_MISMATCH     0x05

/* Protocol v2 capabilities and transfer limits. */
#define NAVI_PROTOCOL_VERSION       0x02
#define NAVI_FEATURE_RAW_ATT_PACKET 0x01
#define NAVI_FEATURE_FRAME_CRC32    0x02
#define NAVI_REQUIRED_FEATURES      (NAVI_FEATURE_RAW_ATT_PACKET | NAVI_FEATURE_FRAME_CRC32)

/* MTU 247 leaves 244 bytes for one complete FFD3 ATT value. */
#define NAVI_MAX_PACKET 244
/* The largest FFD4 REPORT is 104 bytes, which sets the minimum negotiated packet size. */
#define NAVI_MIN_PACKET             (NAVI_L2_HEADER_LEN + 3 + 6 * NAVI_MAX_GAPS)
#define NAVI_FRAME_HEADER_LEN       8
#define NAVI_FRAME_FIRST_HEADER_LEN 12
#define NAVI_MAX_PACKET_DATA        (NAVI_MAX_PACKET - NAVI_FRAME_HEADER_LEN)
#define NAVI_MAX_FIRST_PACKET_DATA  (NAVI_MAX_PACKET - NAVI_FRAME_FIRST_HEADER_LEN)
#define NAVI_FRAME_SLOTS            3
#define NAVI_MAX_JPEG_SIZE          (128 * 1024)
#define NAVI_SESSION_TIMEOUT_MS     10000

/* Session and frame-buffer types. */

/** Navigation projection session state. */
typedef enum
{
    NAVI_STATE_IDLE = 0,
    NAVI_STATE_VALIDATING,
    NAVI_STATE_ACTIVE
} navi_state_t;

/** Negotiated parameters from the OPEN request. */
typedef struct
{
    uint16_t width;
    uint16_t height;
    uint8_t fps;
    uint8_t quality;
    uint8_t flow_ctrl_enable;
    uint8_t protocol_version;
    uint8_t features;
    uint16_t max_packet;
} navi_open_params_t;

/** Missing frame byte range in the half-open interval [gap_start, gap_end). */
typedef struct
{
    uint32_t gap_start;
    uint32_t gap_end;
} navi_gap_t;

#define NAVI_MAX_GAPS 16

typedef enum
{
    NAVI_SLOT_FREE = 0,
    NAVI_SLOT_RECEIVING,
    NAVI_SLOT_READY,
    NAVI_SLOT_USE
} navi_slot_state_t;

/** One slot in the latest-frame-wins triple buffer shared with the GUI task. */
typedef struct
{
    uint16_t frame_seq;
    uint32_t total_len;
    uint32_t received_bytes;
    uint32_t expected_crc32;
    bool crc32_received;
    uint8_t *jpeg_buffer;
    uint8_t *chunk_bitmap;
    uint16_t credits_consumed;
    uint16_t pending_retx_credits;
    bool complete;
    bool terminal_seen;
    bool gui_claimed;
    volatile navi_slot_state_t state;
} navi_frame_slot_t;

/**
 * @brief Receive a complete, validated JPEG frame.
 *
 * @param p_jpeg JPEG payload without the HoneyGUI wrapper.
 * @param jpeg_len JPEG payload length in bytes.
 * @param frame_seq Protocol frame sequence number.
 */
typedef void (*navi_frame_ready_cb_t)(const uint8_t *p_jpeg, uint32_t jpeg_len, uint16_t frame_seq);

/**
 * @brief Install a wrapped JPEG source from the GUI task.
 *
 * @param p_gui_src HoneyGUI image header followed by the JPEG payload.
 * @param jpeg_len JPEG payload length in bytes.
 * @param frame_seq Protocol frame sequence number.
 * @return true if the GUI accepted the source and owns it until the next accepted frame.
 */
typedef bool (*navi_gui_frame_cb_t)(const uint8_t *p_gui_src,
                                    uint32_t jpeg_len,
                                    uint16_t frame_seq);

/* QR-code download URL helpers. */

/** Base URL of the HoneyBox Android download page. */
#define NAVI_QR_URL_BASE    "https://github.com/realmcu/HoneyBox/releases/latest/download/HoneyBox.apk"

/** Model identifier embedded in the QR-code URL. */
#define NAVI_QR_MODEL_ID    "RTL8773G"

/** Serial-number field embedded in the QR-code URL. */
#define NAVI_QR_SN          "0529"

/** Minimum buffer size (bytes) for navi_projection_build_qr_url(), including the NUL terminator. */
#define NAVI_QR_URL_MAX_LEN 256

/** Placeholder BLE address used on simulator hosts (_WIN32 / __linux__), percent-encoded colons. */
#define NAVI_QR_ADDR_SIM    "42%3A18%3A3F%3A91%3A72%3A44"

/**
 * @brief Build the HoneyBox download QR-code URL for this device.
 *
 * Produces a canonical URL of the form:
 * @verbatim
 *   https://github.com/realmcu/HoneyBox/releases/latest/download/HoneyBox.apk
 *   ?modelid=RTL8773G&sn=0529&addr=AA%3ABB%3ACC%3ADD%3AEE%3AFF
 * @endverbatim
 *
 * Colon separators in the BLE address are percent-encoded as %3A so the URL
 * is valid without additional quoting in all QR-code contexts.  Address bytes
 * are ordered most-significant first (standard display order).
 *
 * On simulator hosts (_WIN32 / __linux__) the address field is replaced by
 * the compile-time placeholder #NAVI_QR_ADDR_SIM.
 *
 * Example:
 * @code
 *     char url[NAVI_QR_URL_MAX_LEN];
 *     navi_projection_build_qr_url(url, sizeof(url));
 *     gui_qbcode_config(qrcode, (uint8_t *)url, strlen(url), 3);
 * @endcode
 *
 * @param[out] buf  Caller-supplied buffer that receives the null-terminated URL.
 *                  Must be at least #NAVI_QR_URL_MAX_LEN bytes to avoid truncation.
 * @param[in]  len  Size of @p buf in bytes.
 *
 * @return Number of characters that would have been written excluding the NUL
 *         terminator, following the snprintf convention.  A return value >= @p len
 *         indicates truncation.
 */
int navi_projection_build_qr_url(char *buf, size_t len);

/* Public API. */

/**
 * @brief Initialize and register the navigation projection GATT service.
 *
 * @param service_cb Generic extended-server callback, or NULL if it is not needed.
 * @param frame_cb Callback for complete JPEG frames, or NULL to disable it.
 * @return Registered service ID, or 0xFF if registration fails.
 */
uint8_t navi_projection_init(void *service_cb, navi_frame_ready_cb_t frame_cb);

/**
 * @brief Get the current projection session state.
 *
 * @return Current session state.
 */
navi_state_t navi_projection_get_state(void);

/**
 * @brief Close the current session and release its frame slots.
 *
 * An active session is notified with a timeout error before local state is reset.
 */
void navi_projection_close(void);

/**
 * @brief Get the registered GATT service ID.
 *
 * @return Registered service ID, or 0xFF before successful initialization.
 */
uint8_t navi_projection_get_service_id(void);

/**
 * @brief Set the callback for validated JPEG payloads.
 *
 * @param frame_cb New callback, or NULL to disable it.
 */
void navi_projection_set_frame_cb(navi_frame_ready_cb_t frame_cb);

/**
 * @brief Set the GUI-task callback that installs a wrapped JPEG source.
 *
 * @param frame_cb New callback, or NULL to reject queued GUI frames.
 */
void navi_projection_set_gui_frame_cb(navi_gui_frame_cb_t frame_cb);

#ifdef __cplusplus
}
#endif
#endif /* _NAVI_PROJECTION_H_ */
