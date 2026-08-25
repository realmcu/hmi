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
 *                              Keys - settings (0x02)
 *============================================================================*/

#define HMI_L2_SET_TIME         0x01u
#define HMI_L2_SET_STEP_TARGET  0x05u
#define HMI_L2_SET_USER_PROFILE 0x10u
#define HMI_L2_SET_ANTI_LOST    0x20u
#define HMI_L2_SET_SEDENTARY    0x21u
#define HMI_L2_SET_HANDEDNESS   0x22u
#define HMI_L2_SET_PHONE_OS     0x23u
#define HMI_L2_SET_CALL_LIST    0x24u
#define HMI_L2_SET_CALL_SWITCH  0x25u

/*============================================================================*
 *                              Keys - bind (0x03)
 *============================================================================*/

#define HMI_L2_BIND_REQ         0x01u
#define HMI_L2_BIND_RSP         0x02u
#define HMI_L2_LOGIN_REQ        0x03u
#define HMI_L2_LOGIN_RSP        0x04u
#define HMI_L2_UNBIND           0x05u
#define HMI_L2_SUPER_BIND_REQ   0x06u
#define HMI_L2_SUPER_BIND_RSP   0x07u

/*============================================================================*
 *                              Keys - notify (0x04)
 *============================================================================*/

#define HMI_L2_CALL_RING        0x01u
#define HMI_L2_CALL_ANSWER      0x02u
#define HMI_L2_CALL_REJECT      0x03u

/*============================================================================*
 *                              Keys - sport (0x05)
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

/* SPORT L2 Header byte 1: Version=1 in [7:4], Reserve=0 in [3:0].
 * Unlike every other command (which is still version 0), SPORT version 1
 * packets MUST carry 0x10 here -- see spec 3.6 "版本与通用约束". */
#define HMI_L2_SPORT_VER        0x10u

/* Data type carried by SPORT_REQ / SPORT_MORE / SYNC_START / SYNC_END */
#define HMI_L2_SPORT_DT_ACTIVITY 0x01u  /* 15-min activity buckets -> key 0x02 */
#define HMI_L2_SPORT_DT_SLEEP    0x02u  /* sleep state changes     -> key 0x03 */

/* Wire size of one Sport bucket record, and the per-page record cap.
 * A full page is 1 + 8*18 = 145 value bytes (150-byte L2 packet). */
#define HMI_L2_SPORT_REC_SIZE    18u
#define HMI_L2_SPORT_RECS_MAX    8u

/*============================================================================*
 *                              Keys - control (0x07)
 *============================================================================*/

#define HMI_L2_CTRL_PHOTO       0x01u
#define HMI_L2_CTRL_SINGLE_TAP  0x02u
#define HMI_L2_CTRL_DOUBLE_TAP  0x03u
#define HMI_L2_CTRL_CAMERA_ST   0x11u

/*============================================================================*
 *                              Keys - log (0x0a)
 *============================================================================*/

#define HMI_L2_LOG_OPEN         0x01u
#define HMI_L2_LOG_CLOSE        0x02u
#define HMI_L2_LOG_SEND         0x03u

/*============================================================================*
 *                              Keys - BLE connection parameters (0x0c)
 *============================================================================*/

#define HMI_L2_CMD_CONN_PARAM       0x0cu   /* BLE connection parameters */

#define HMI_L2_CONN_PARAM_REQ       0x01u   /* query request  (phone -> device) */
#define HMI_L2_CONN_PARAM_RSP       0x02u   /* query response (device -> phone) */

/*============================================================================*
 *                              Keys - WiFi provisioning (0x0d)
 *============================================================================*/

#define HMI_L2_CMD_WIFI_PROV        0x0du   /* WiFi provisioning */

#define HMI_L2_WIFI_CONFIG_SET      0x01u   /* push SSID/password      (phone -> device) */
#define HMI_L2_WIFI_CONFIG_ACK      0x02u   /* accept / reject         (device -> phone) */
#define HMI_L2_WIFI_STATUS_REQ      0x03u   /* poll current state      (phone -> device) */
#define HMI_L2_WIFI_STATUS          0x04u   /* state report / IP:port  (device -> phone) */

/* WIFI_CONFIG_ACK result */
#define HMI_L2_WIFI_ACK_ACCEPTED    0x00u
#define HMI_L2_WIFI_ACK_REJECTED    0x01u

/* WIFI_CONFIG_ACK error codes */
#define HMI_L2_WIFI_ERR_NONE        0x00u
#define HMI_L2_WIFI_ERR_MALFORMED   0x01u
#define HMI_L2_WIFI_ERR_UNSUPPORTED 0x02u
#define HMI_L2_WIFI_ERR_INVALID_SSID 0x03u
#define HMI_L2_WIFI_ERR_INVALID_PWD 0x04u
#define HMI_L2_WIFI_ERR_BUSY        0x05u

/* WIFI_STATUS state codes */
#define HMI_L2_WIFI_STATE_IDLE      0x00u
#define HMI_L2_WIFI_STATE_CONNECTING 0x01u
#define HMI_L2_WIFI_STATE_CONNECTED 0x02u
#define HMI_L2_WIFI_STATE_FAILED    0x03u

/* WIFI_STATUS error codes (state = FAILED) */
#define HMI_L2_WIFI_STATUS_ERR_NONE    0x00u
#define HMI_L2_WIFI_STATUS_ERR_AUTH    0x01u   /* wrong SSID / password */
#define HMI_L2_WIFI_STATUS_ERR_NO_AP   0x02u   /* AP not found */
#define HMI_L2_WIFI_STATUS_ERR_DHCP    0x03u
#define HMI_L2_WIFI_STATUS_ERR_TIMEOUT 0x04u
#define HMI_L2_WIFI_STATUS_ERR_TCP     0x05u   /* TCP server start failed */
#define HMI_L2_WIFI_STATUS_ERR_UNKNOWN 0x06u

/* WIFI_CONFIG_SET flag bits */
#define HMI_L2_WIFI_FLAG_SAVE_CRED  0x01u   /* persist credentials on device */

/*============================================================================*
 *                              Keys - file transfer (0x0b)
 *============================================================================*/

// #define HMI_L2_CMD_FILE_XFER        0x0bu   /* file transfer */
#define HMI_L2_CMD_FILE_XFER        0x10u   /* file transfer */

#define HMI_L2_XFER_BEGIN_REQ       0x01u   /* session open request  (phone -> device) */
#define HMI_L2_XFER_BEGIN_RSP       0x02u   /* session open response (device -> phone) */
#define HMI_L2_XFER_DATA            0x03u   /* data chunk            (phone -> device) */
#define HMI_L2_XFER_END_REQ         0x05u   /* transfer end request  (phone -> device) */
#define HMI_L2_XFER_END_RSP         0x06u   /* transfer end response (device -> phone) */
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
 *                              Keys - stream (0x0e)
 *============================================================================*/

#define HMI_L2_CMD_STREAM           0x0eu   /* live video stream */

#define HMI_L2_KS_OPEN              0x01u   /* App -> Dev: open stream session        */
#define HMI_L2_KS_ACK               0x02u   /* Dev -> App: confirm open / error       */
#define HMI_L2_KS_FRAME             0x03u   /* App -> Dev: frame data chunk           */
#define HMI_L2_KS_CLOSE             0x04u   /* App -> Dev: close stream session       */
#define HMI_L2_KS_CREDIT            0x05u   /* Dev -> App: credit grant (flow ctrl)   */
#define HMI_L2_KS_REPORT            0x06u   /* Dev -> App: frame reception status     */

/* KS_OPEN codec field */
#define HMI_L2_KS_CODEC_MSV1        0x00u   /* RGB555 16bpp, MSV1 compatible    */
#define HMI_L2_KS_CODEC_JPEG        0x01u   /* standard JPEG byte stream        */
#define HMI_L2_KS_CODEC_H264        0x02u   /* baselined H264 byte stream       */

/* KS_ACK result field */
#define HMI_L2_KS_ACK_OK            0x00u
#define HMI_L2_KS_ACK_BAD_CODEC     0x01u
#define HMI_L2_KS_ACK_BUSY          0x02u   /* device busy, retry later         */

/* KS_ACK init_credits: device RX buffer depth in KS_FRAME packets;
 * 0xFFFF = no flow control (fire-and-forget fallback) */
#define HMI_L2_KS_CREDITS_UNLIMITED 0xFFFFu

/*============================================================================*
 *                       Keys - remote control (0x0f)  (spec v2)
 *============================================================================*
 * Bidirectional camera control (see hmi-android-apk spec
 * 2026-07-30-remote-control-protocol-design.md).
 *   0x01-0x0F  control requests (Dev -> App;  app is authority, may clamp/reject)
 *   0x10-0x1F  state facts      (App -> Dev;  weathervane -- device only consumes)
 *   0x20-0x2F  preview pull triggers (not in P0)
 *
 * Device rules (spec section 8 -- loopback / consistency):
 *   - never optimistically update local UI on outbound control; wait for STATE_REPORT
 *   - drop any inbound key in 0x01-0x0F (app never sends control to device)
 *   - unknown TLV tags in STATE_REPORT are silently skipped (forward compat)
 *
 * P0 covers CAPTURE + SET_ZOOM + a minimal STATE/CTRL_RESULT/LAST_SHOT_READY set. */

#define HMI_L2_CMD_REMOTE               0x0fu

/* Control requests (Dev -> App) */
#define HMI_L2_RC_CAPTURE               0x01u   /* vlen=0                             */
#define HMI_L2_RC_SET_ZOOM              0x04u   /* 4B: zoom_x100 u16be + reserved u16 */

/* State facts (App -> Dev) */
#define HMI_L2_RC_STATE_REPORT          0x10u   /* TLV (see spec 5.2)                 */
#define HMI_L2_RC_CTRL_RESULT           0x11u   /* 3B: key + code + detail            */
#define HMI_L2_RC_LAST_SHOT_READY       0x12u   /* 3B: shot_id u16be + reserved       */

/* CTRL_RESULT error codes (spec section 7 -- authoritative byte values) */
#define HMI_L2_RC_ERR_UNSUPPORTED       0x01u
#define HMI_L2_RC_ERR_BUSY              0x02u
#define HMI_L2_RC_ERR_OUT_OF_RANGE      0x03u   /* spec 12 writes "INVALID_VALUE" but
                                                 * the byte value matches section 7 */
#define HMI_L2_RC_ERR_NOT_READY         0x04u
#define HMI_L2_RC_ERR_UNKNOWN           0xffu

/* STATE_REPORT TLV tags -- P0 recognizes these five; others silently skipped */
#define HMI_L2_RC_TAG_RECORDING         0x01u   /* 1B  */
#define HMI_L2_RC_TAG_FACING            0x03u   /* 1B  */
#define HMI_L2_RC_TAG_ZOOM_X100         0x04u   /* 2B be */
#define HMI_L2_RC_TAG_HAS_LAST_SHOT     0x0fu   /* 1B  */
#define HMI_L2_RC_TAG_LAST_SHOT_ID      0x10u   /* 2B be */

/*============================================================================*
 *                              KV entry (exposed for handlers)
 *============================================================================*/

typedef struct
{
    uint8_t        key;
    uint16_t       val_len;
    const uint8_t *val;
} hmi_l2_kv_t;

/*============================================================================*
 *                              API
 *============================================================================*/

typedef void (*hmi_l2_cmd_handler_t)(const hmi_l2_kv_t *kvs, uint8_t n);

/**
 * @brief  Register a handler for a given L2 command ID.
 *         Call before the first packet arrives (e.g. during system init).
 */
void hmi_l2_register(uint8_t cmd_id, hmi_l2_cmd_handler_t handler);

/**
 * @brief  Parse an L2 packet and dispatch to the registered command handler.
 *
 * @param data  Pointer to the L1 payload (i.e. the full L2 packet: L2 header + L2 payload).
 * @param len   Data length; must be at least 2 bytes (L2 header).
 */
void hmi_l2_handle(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_H_ */
