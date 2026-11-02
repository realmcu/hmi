#ifndef _HMI_L2_CMD_XFER_H_
#define _HMI_L2_CMD_XFER_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * @brief  Register the file-transfer L2 command handler and subscribe to
 *         EVT_BLE_DISCONNECTED (so a link loss aborts a half-written file).
 *
 * Called once during app bring-up (see hmi_l2_handlers_register).
 */
void hmi_l2_xfer_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_XFER_H_ */
