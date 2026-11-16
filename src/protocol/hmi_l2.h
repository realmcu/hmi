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

#include <stdbool.h>
#include <stdint.h>

/*============================================================================*
 *                      HoneyProtocol L2 -- command router
 *
 *  Spec: https://realmcu.github.io/HoneyProtocol/  section 2.4 / chapter 3
 *
 *  One frame carries exactly one command:
 *
 *      [0] cmd  [1] version/rsv  [2] key  [3..4] value length (BE)  [5..] value
 *
 *  Commands the application owns (file transfer 0x10 and the projection
 *  session extension 0x11) are offered to a registered handler first; the
 *  remaining command IDs fall through to the built-in handlers below.
 *============================================================================*/

/*---------------------------------------------------------------------------*
 *                              L2 Command IDs
 *---------------------------------------------------------------------------*/

#define HMI_L2_CMD_OTA          0x01u   /* firmware upgrade */
#define HMI_L2_CMD_DATA         0x05u   /* data command (spec 3.5) */
#define HMI_L2_CMD_SETTINGS     0x02u   /* device settings */
#define HMI_L2_CMD_BIND         0x03u   /* bind / login */
#define HMI_L2_CMD_NOTIFY       0x04u   /* notifications (call, message, etc.) */
#define HMI_L2_CMD_SPORT        0x05u   /* sport / health data */
#define HMI_L2_CMD_FACTORY      0x06u   /* factory test */
#define HMI_L2_CMD_CONTROL      0x07u   /* device control */
#define HMI_L2_CMD_DUMP_STACK   0x08u   /* dump stack */
#define HMI_L2_CMD_TEST_FLASH   0x09u   /* flash read test */
#define HMI_L2_CMD_LOG          0x0au   /* log transfer */
#define HMI_L2_CMD_FILE_XFER    0x10u   /* file transfer (spec 3.12) */
#define HMI_L2_CMD_POINT_TEST   0x11u   /* point test (spec 3.13) */

/*---------------------------------------------------------------------------*
 *        Keys -- data command 0x05: device information (projection)
 *
 *  Follows the spec's key convention: the request lives in the 0x00..0x1F
 *  "sync request" block and the answer is request + 0x20.
 *---------------------------------------------------------------------------*/

#define HMI_L2_DATA_DEV_INFO_REQ 0x0Bu   /* phone -> device, value empty */
#define HMI_L2_DATA_DEV_INFO_RSP 0x2Bu   /* device -> phone, TLV list */

/* TLV ids used by the device information answer. Unknown ids are ignored by
 * the phone, so new fields can be appended without a version bump. */
#define HMI_L2_INFO_ID_AREA      0x01u   /* u16 width, u16 height           */
#define HMI_L2_INFO_ID_JPEG_MAX  0x02u   /* u32 max JPEG bytes              */
#define HMI_L2_INFO_ID_QUALITY   0x03u   /* u8 recommended JPEG quality      */
#define HMI_L2_INFO_ID_FEATURES  0x04u   /* u32 capability bitmap           */
#define HMI_L2_INFO_ID_CHUNK_MAX 0x05u   /* u16 max chunk bytes             */
#define HMI_L2_INFO_ID_FPS_REQ   0x06u   /* u8  fps the device asks for     */

/* Feature bits */
#define HMI_L2_FEAT_PROJECTION   0x00000001u
#define HMI_L2_FEAT_QR_PAIRING   0x00000002u
#define HMI_L2_FEAT_DEVICE_CHECK 0x00000004u  /* device validates CRC32 + JPEG */

/*---------------------------------------------------------------------------*
 *                              Keys -- settings (0x02)
 *---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*
 *                              Keys -- bind (0x03)
 *---------------------------------------------------------------------------*/

#define HMI_L2_BIND_REQ         0x01u
#define HMI_L2_BIND_RSP         0x02u
#define HMI_L2_LOGIN_REQ        0x03u
#define HMI_L2_LOGIN_RSP        0x04u
#define HMI_L2_UNBIND           0x05u
#define HMI_L2_SUPER_BIND_REQ   0x06u
#define HMI_L2_SUPER_BIND_RSP   0x07u

/*---------------------------------------------------------------------------*
 *                              Keys -- notify (0x04)
 *---------------------------------------------------------------------------*/

#define HMI_L2_CALL_RING        0x01u
#define HMI_L2_CALL_ANSWER      0x02u
#define HMI_L2_CALL_REJECT      0x03u

/*---------------------------------------------------------------------------*
 *                              Keys -- sport (0x05)
 *---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*
 *                              Keys -- control (0x07)
 *---------------------------------------------------------------------------*/

#define HMI_L2_CTRL_PHOTO       0x01u
#define HMI_L2_CTRL_SINGLE_TAP  0x02u
#define HMI_L2_CTRL_DOUBLE_TAP  0x03u
#define HMI_L2_CTRL_CAMERA_ST   0x11u

/*---------------------------------------------------------------------------*
 *                              Keys -- log (0x0a)
 *---------------------------------------------------------------------------*/

#define HMI_L2_LOG_OPEN         0x01u
#define HMI_L2_LOG_CLOSE        0x02u
#define HMI_L2_LOG_SEND         0x03u

/*---------------------------------------------------------------------------*
 *                    Keys -- file transfer (0x10, spec 3.12)
 *---------------------------------------------------------------------------*/

#define HMI_L2_XFER_BEGIN_REQ       0x01u   /* phone -> device: type/size/chunk/name */
#define HMI_L2_XFER_BEGIN_RSP       0x02u   /* device -> phone: status + chunk */
#define HMI_L2_XFER_DATA            0x03u   /* phone -> device: seq + bytes */
#define HMI_L2_XFER_END_REQ         0x05u   /* phone -> device: whole-file CRC32 */
#define HMI_L2_XFER_END_RSP         0x06u   /* device -> phone: status + error */
#define HMI_L2_XFER_ABORT           0x07u   /* either direction: reason */

/* file_type values (BEGIN_REQ) */
#define HMI_L2_XFER_TYPE_IMAGE      0x01u
#define HMI_L2_XFER_TYPE_VIDEO      0x02u
#define HMI_L2_XFER_TYPE_RAW        0x03u

/* BEGIN_RSP status */
#define HMI_L2_XFER_BEGIN_OK        0x00u
#define HMI_L2_XFER_BEGIN_BUSY      0x01u
#define HMI_L2_XFER_BEGIN_NO_SPACE  0x02u
#define HMI_L2_XFER_BEGIN_BAD_TYPE  0x03u

/* END_RSP status */
#define HMI_L2_XFER_END_OK          0x00u
#define HMI_L2_XFER_END_CRC_FAIL    0x01u
#define HMI_L2_XFER_END_INCOMPLETE  0x02u

/* END_RSP error_code */
#define HMI_L2_XFER_ERR_NONE        0x00u
#define HMI_L2_XFER_ERR_WRITE_FAIL  0x01u
#define HMI_L2_XFER_ERR_DECODE_FAIL 0x02u

/* ABORT reason */
#define HMI_L2_XFER_ABORT_USER      0x00u
#define HMI_L2_XFER_ABORT_ERROR     0x01u
#define HMI_L2_XFER_ABORT_TIMEOUT   0x02u

/* Chunk size: requested range per spec, and the compile-time ceiling that a
 * single L1 frame can carry (248 - 5 - 2). The value actually used is the one
 * returned in BEGIN_RSP, negotiated down to the current MTU. */
#define HMI_L2_XFER_CHUNK_REQ_MAX   2048u
#define HMI_L2_XFER_CHUNK_MAX       241u

/*---------------------------------------------------------------------------*
 *                              API
 *---------------------------------------------------------------------------*/

/**
 * @brief Handler for command IDs owned by the application.
 *
 * Registered by the projection engine, which owns 0x10 (file transfer) and the
 * 0x11 session extension. Returning true means the frame was consumed and no
 * built-in handler runs.
 */
typedef bool (*hmi_l2_app_fn_t)(uint8_t cmd, uint8_t key,
                                const uint8_t *value, uint16_t len);

/** Values reported by 0x05 / 0x0B. All fields are limits, not current values. */
typedef struct
{
    uint16_t stream_width;      /* the projection widget's own geometry  */
    uint16_t stream_height;
    uint32_t jpeg_max_bytes;
    uint8_t  quality;           /* JPEG quality that fits jpeg_max_bytes */
    uint32_t features;          /* HMI_L2_FEAT_* bitmap                  */
    uint16_t chunk_max;         /* largest DATA chunk the device accepts */
    uint8_t  requested_fps;     /* sampling rate the device wants        */
} hmi_l2_device_info_t;

/** Fills @p out with the values to report. Returns false when unavailable. */
typedef bool (*hmi_l2_info_fn_t)(hmi_l2_device_info_t *out);

/**
 * @brief Register the application handler and the device-information provider.
 *        Either may be NULL.
 */
void hmi_l2_init(hmi_l2_app_fn_t app, hmi_l2_info_fn_t info);

/**
 * @brief Route one complete L2 frame (5-byte header + value).
 *
 * Called from the L1 layer with a borrowed pointer; must not block.
 */
void hmi_l2_handle(const uint8_t *l2, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_H_ */
