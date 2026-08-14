/**
 * @file    ebadge_errcode.h
 * @brief   Result and failure codes carried in Notify payloads.
 *
 * Spec: eBadge-PROT-001 V1.2 §2.5 (generic result), §2.6 (xfer errors).
 */
#ifndef _EBADGE_ERRCODE_H_
#define _EBADGE_ERRCODE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Generic RESULT codes  (spec §2.5, carried in 0x04 EB_TLV_RESULT_CODE)
 *
 *  CAUTION: the spec defines exactly two values and 0x00 means FAILURE.
 *  Do not add "detail" codes here -- §2.5 has no room for them.  When a
 *  H->D command fails, answer FAILED; put any nuance in the log, not the
 *  wire.  (The pre-V1.2-alignment code had OK=0x00 which the App read as
 *  a failure on every successful SET_TIME / SEND_MSG.)
 *----------------------------------------------------------------------------*/
#define EB_RESULT_FAILED          0x00    /* VS_CMD_FAILED                    */
#define EB_RESULT_SUCCEED         0x01    /* VS_CMD_SUCCEED                   */

/*----------------------------------------------------------------------------*
 *  0x11 XFER_DECISION decision value  (spec §4.6)
 *----------------------------------------------------------------------------*/
#define EB_DECISION_REJECT        0x00
#define EB_DECISION_ACCEPT        0x01
#define EB_DECISION_TIMEOUT       0x02

/*----------------------------------------------------------------------------*
 *  Transfer error codes  (spec §2.6)
 *
 *  Used both as 0x11 DECISION's optional reason TLV and as 0x16 FAIL's
 *  required reason TLV, and as the EBXR ack reason byte (spec §5.3).
 *----------------------------------------------------------------------------*/
#define EB_XFER_ERR_USER_REJECT     0x01   /* user declined                   */
#define EB_XFER_ERR_USER_TIMEOUT    0x02   /* no confirmation within 30s      */
#define EB_XFER_ERR_STORAGE_FULL    0x03
#define EB_XFER_ERR_FMT_UNSUPPORTED 0x04   /* file_type not supported         */
#define EB_XFER_ERR_TOO_LARGE       0x05   /* exceeds single-file cap         */
#define EB_XFER_ERR_AP_START        0x06   /* SoftAP or listener start failed */
#define EB_XFER_ERR_STA_TIMEOUT     0x07   /* phone never joined the AP       */
#define EB_XFER_ERR_IO_TIMEOUT      0x08   /* transfer stalled / interrupted  */
#define EB_XFER_ERR_VERIFY          0x09   /* length or CRC32 mismatch        */
#define EB_XFER_ERR_BUSY            0x0A   /* another transfer in progress    */
#define EB_XFER_ERR_CANCELLED       0x0B   /* App or device cancelled (resv)  */

/*----------------------------------------------------------------------------*
 *  Local error codes  (returned by ebadge_* APIs; never put on the wire)
 *----------------------------------------------------------------------------*/
typedef enum
{
    EBADGE_OK              = 0,
    EBADGE_ERR_PARAM       = -1,
    EBADGE_ERR_NOMEM       = -2,
    EBADGE_ERR_NO_LINK     = -3,   /* BLE not connected              */
    EBADGE_ERR_NO_CCCD     = -4,   /* Peer did not subscribe TX      */
    EBADGE_ERR_BUSY        = -5,
    EBADGE_ERR_TIMEOUT     = -6,
    EBADGE_ERR_FRAME       = -7,   /* Malformed frame                */
    EBADGE_ERR_TLV         = -8,   /* Malformed TLV                  */
    EBADGE_ERR_UNKNOWN_CMD = -9,
    EBADGE_ERR_UNSUPPORTED = -10,
    EBADGE_ERR_INTERNAL    = -99,
} ebadge_status_t;

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_ERRCODE_H_ */
