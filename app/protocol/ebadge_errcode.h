/**
 * @file    ebadge_errcode.h
 * @brief   Result and failure codes carried in Notify payloads.
 *
 * Spec: eBadge-PROT-001 V1.3 §2.5 (generic result), §2.6 (xfer errors).
 * The §2.6 table is unchanged from V1.2 and is reused verbatim by the V1.3
 * stream path (0x09 reason TLV, 0x16 on a stream abort).
 *
 * §2.5 is NOT unchanged -- see the polarity warning on EB_RESULT_* below.
 */
#ifndef _EBADGE_ERRCODE_H_
#define _EBADGE_ERRCODE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Generic RESULT codes  (spec §2.5, carried in 0x04 EB_TLV_RESULT_CODE)
 *
 *  !! WIRE-BREAKING CHANGE IN V1.3 -- THE POLARITY IS INVERTED !!
 *
 *  V1.2 §2.5 had   0x00 = FAILED, 0x01 = SUCCEED.
 *  V1.3 §2.5 has   0x00 = SUCCEED, 0x01 = FAILED, plus 0x02 / 0x03.
 *
 *  So the very same byte that used to mean "it worked" now means "it
 *  failed".  The App MUST be updated in lockstep with this firmware or
 *  every reply is read inverted -- there is no version negotiation in the
 *  frame header we could switch on.
 *
 *  KNOWN SPEC DEFECT: the §4.1 and §4.2 hex examples were NOT updated when
 *  the §2.5 table was flipped -- both still send 0x01 to mean success (they
 *  are unchanged V1.0 text).  We follow the §2.5 TABLE, which is the
 *  normative definition and the thing this revision deliberately changed;
 *  the examples are stale.  Confirm with the App team before hardware
 *  bring-up, because if they coded to the examples we are inverted again.
 *----------------------------------------------------------------------------*/
#define EB_RESULT_SUCCEED         0x00    /* VS_CMD_SUCCEED                   */
#define EB_RESULT_FAILED          0x01    /* VS_CMD_FAILED                    */
#define EB_RESULT_NOT_READY       0x02    /* VS_CMD_NOT_READY (V1.3)          */
#define EB_RESULT_BUSY            0x03    /* VS_CMD_BUSY      (V1.3)          */

/*----------------------------------------------------------------------------*
 *  DECISION values -- shared by 0x11 XFER_DECISION (spec §4.8) and
 *  0x09 JPG_STREAM_DECISION (spec §4.6, V1.3)
 *
 *  0x00 / 0x01 mean the same thing in both.  0x02 does NOT: in 0x11 it is
 *  "the user did not answer in time", in 0x09 it is "accepted, but at the
 *  frame rate I am telling you in TLV_SDEC_FPS".  Two names, one value, so a
 *  call site reads as what it means -- never mix them up.
 *----------------------------------------------------------------------------*/
#define EB_DECISION_REJECT        0x00
#define EB_DECISION_ACCEPT        0x01
#define EB_DECISION_TIMEOUT       0x02   /* 0x11 only: no user answer in 30s */
#define EB_STREAM_DEC_NEGOTIATE   0x02   /* 0x09 only: accepted at other fps */

/*----------------------------------------------------------------------------*
 *  Transfer error codes  (spec §2.6)
 *
 *  Used both as 0x11 DECISION's optional reason TLV and as 0x16 FAIL's
 *  required reason TLV, and as the EBXR ack reason byte (spec §5.3).
 *  V1.3 adds one more consumer: 0x09 STREAM_DECISION's reason TLV.
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
