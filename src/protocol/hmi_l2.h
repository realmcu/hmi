/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _HMI_L2_H_
#define _HMI_L2_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================================================================*
 *                              L2 Command IDs
 *============================================================================*/

#define HMI_L2_CMD_OTA          0x01u   /* firmware upgrade */
#define HMI_L2_CMD_SETTINGS     0x02u   /* device settings */
#define HMI_L2_CMD_BIND         0x03u   /* bind / login */
#define HMI_L2_CMD_NOTIFY       0x04u   /* notifications (call, message, etc.) */
#define HMI_L2_CMD_SPORT        0x05u   /* sport / health data */
#define HMI_L2_CMD_FACTORY      0x06u   /* factory test */
#define HMI_L2_CMD_CONTROL      0x07u   /* device control */
#define HMI_L2_CMD_DUMP_STACK   0x08u   /* dump stack (not yet implemented) */
#define HMI_L2_CMD_TEST_FLASH   0x09u   /* flash read test (not yet implemented) */
#define HMI_L2_CMD_LOG          0x0au   /* log transfer */

/*============================================================================*
 *                              Keys — settings (0x02)
 *============================================================================*/

#define HMI_L2_SET_TIME         0x01u
#define HMI_L2_SET_ALARM        0x02u
#define HMI_L2_GET_ALARM_REQ    0x03u
#define HMI_L2_GET_ALARM_RSP    0x04u
#define HMI_L2_SET_STEP_TARGET  0x05u
#define HMI_L2_SET_USER_PROFILE 0x10u
#define HMI_L2_SET_ANTI_LOST    0x20u
#define HMI_L2_SET_SEDENTARY    0x21u
#define HMI_L2_SET_HANDEDNESS   0x22u
#define HMI_L2_SET_PHONE_OS     0x23u
#define HMI_L2_SET_CALL_LIST    0x24u
#define HMI_L2_SET_CALL_SWITCH  0x25u

/*============================================================================*
 *                              Keys — bind (0x03)
 *============================================================================*/

#define HMI_L2_BIND_REQ         0x01u
#define HMI_L2_BIND_RSP         0x02u
#define HMI_L2_LOGIN_REQ        0x03u
#define HMI_L2_LOGIN_RSP        0x04u
#define HMI_L2_UNBIND           0x05u
#define HMI_L2_SUPER_BIND_REQ   0x06u
#define HMI_L2_SUPER_BIND_RSP   0x07u

/*============================================================================*
 *                              Keys — notify (0x04)
 *============================================================================*/

#define HMI_L2_CALL_RING        0x01u
#define HMI_L2_CALL_ANSWER      0x02u
#define HMI_L2_CALL_REJECT      0x03u

/*============================================================================*
 *                              Keys — sport (0x05)
 *============================================================================*/

#define HMI_L2_SPORT_REQ        0x01u
#define HMI_L2_SPORT_DATA_RSP   0x02u
#define HMI_L2_SLEEP_DATA_RSP   0x03u
#define HMI_L2_SPORT_MORE       0x04u
#define HMI_L2_SLEEP_SET_RSP    0x05u
#define HMI_L2_SPORT_REALTIME   0x06u
#define HMI_L2_SYNC_START       0x07u
#define HMI_L2_SYNC_END         0x08u
#define HMI_L2_TODAY_SPORT_SYNC 0x09u
#define HMI_L2_LAST_SPORT_SYNC  0x0au
#define HMI_L2_CALIBRATE_REQ    0x0bu
#define HMI_L2_CALIBRATE_RSP    0x0cu

/*============================================================================*
 *                              Keys — control (0x07)
 *============================================================================*/

#define HMI_L2_CTRL_PHOTO       0x01u
#define HMI_L2_CTRL_SINGLE_TAP  0x02u
#define HMI_L2_CTRL_DOUBLE_TAP  0x03u
#define HMI_L2_CTRL_CAMERA_ST   0x11u

/*============================================================================*
 *                              Keys — log (0x0a)
 *============================================================================*/

#define HMI_L2_LOG_OPEN         0x01u
#define HMI_L2_LOG_CLOSE        0x02u
#define HMI_L2_LOG_SEND         0x03u

/*============================================================================*
 *                              Keys — file transfer (0x0b)
 *============================================================================*/

#define HMI_L2_CMD_FILE_XFER        0x0bu   /* file transfer */

#define HMI_L2_XFER_BEGIN_REQ       0x01u   /* session open request  (phone → device) */
#define HMI_L2_XFER_BEGIN_RSP       0x02u   /* session open response (device → phone) */
#define HMI_L2_XFER_DATA            0x03u   /* data chunk            (phone → device) */
#define HMI_L2_XFER_END_REQ         0x05u   /* transfer end request  (phone → device) */
#define HMI_L2_XFER_END_RSP         0x06u   /* transfer end response (device → phone) */
#define HMI_L2_XFER_ABORT           0x07u   /* abort (either direction) */

/* file_type values (XFER_BEGIN_REQ) */
#define HMI_L2_XFER_TYPE_IMAGE      0x01u
#define HMI_L2_XFER_TYPE_VIDEO      0x02u
#define HMI_L2_XFER_TYPE_RAW        0x03u

/* XFER_BEGIN_RSP status */
#define HMI_L2_XFER_BEGIN_OK        0x00u
#define HMI_L2_XFER_BEGIN_BUSY      0x01u
#define HMI_L2_XFER_BEGIN_NO_SPACE  0x02u
#define HMI_L2_XFER_BEGIN_BAD_TYPE  0x03u

/* XFER_END_RSP status */
#define HMI_L2_XFER_END_OK          0x00u
#define HMI_L2_XFER_END_CRC_FAIL    0x01u
#define HMI_L2_XFER_END_INCOMPLETE  0x02u

/* XFER_END_RSP error_code */
#define HMI_L2_XFER_ERR_NONE        0x00u
#define HMI_L2_XFER_ERR_WRITE_FAIL  0x01u
#define HMI_L2_XFER_ERR_DECODE_FAIL 0x02u

/* XFER_ABORT reason */
#define HMI_L2_XFER_ABORT_USER      0x00u
#define HMI_L2_XFER_ABORT_ERROR     0x01u
#define HMI_L2_XFER_ABORT_TIMEOUT   0x02u

/* Maximum chunk size (bytes); constrained by L2 payload limit */
#define HMI_L2_XFER_CHUNK_MAX       2048u

/*============================================================================*
 *                              API
 *============================================================================*/

/**
 * @brief  Parse an L2 packet and dispatch to the appropriate command handler.
 *
 * @param data  Pointer to the L1 payload (i.e. the full L2 packet: L2 header + L2 payload).
 * @param len   Data length; must be at least 2 bytes (L2 header).
 */
void hmi_l2_handle(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_H_ */
