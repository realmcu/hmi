/**
 * @file    ebadge_cmd.h
 * @brief   eBadge Protocol command IDs and TLV type codes.
 *
 * Wire spec:  eBadge-PROT-001 V1.3 (2026-08-13).  Frame header is fixed 5B,
 * LE byte order, no CRC / no ACK; reliability is ATT-native.
 *
 * V1.3 deltas implemented here:  the 0x08/0x09 JPEG-stream pair (§4.5/§4.6)
 * plus its 14-byte TCP frame (§6.2, see ebxs_frame.h), and the §2.7 file-type
 * enum grown to 0x07.  V1.3 also drops the confirmation dialog: both 0x08 and
 * 0x10 are answered by the device itself, without user interaction.
 *
 *   [ver 1][cmd 1][0x80][params_len 2 LE][params N]
 *
 * TLV inside params:
 *
 *   [type 1][length 2 LE][val N]
 *
 * IMPORTANT -- TLV namespace (spec §2.4):
 *
 *   "type | 在该命令上下文内解释（不同命令可复用相同 type 号）"
 *
 * TLV type numbers are scoped PER COMMAND and every command restarts at
 * 0x01.  There is deliberately NO global TLV table here: EB_TLV_BAT_PERCENT
 * and EB_TLV_STOR_TOTAL and EB_TLV_XFER_NAME are all 0x01, each valid only
 * inside its own command.  Always use the prefixed macro that matches the
 * command you are encoding/decoding -- the prefix IS the namespace:
 *
 *   EB_TLV_DTIME_*  0x01 SET_TIME          EB_TLV_AP_*     0x13 AP_INFO
 *   EB_TLV_SFILE_*  0x02 SEND_FILE         EB_TLV_PROG_*   0x14 PROGRESS
 *   EB_TLV_MSG_*    0x03 SEND_MSG          EB_TLV_DONE_*   0x15 XFER_DONE
 *   EB_TLV_RESULT_* 0x04 RESULT            EB_TLV_FAIL_*   0x16 XFER_FAIL
 *   EB_TLV_SOFR_*   0x08 STREAM_OFFER      EB_TLV_BAT_*    0x18 BATTERY
 *   EB_TLV_SDEC_*   0x09 STREAM_DECISION   EB_TLV_STOR_*   0x1A STORAGE_INFO
 *   EB_TLV_XFER_*   0x10 XFER_OFFER        EB_TLV_OTA_*    0xE0 OTA_OFFER
 *   EB_TLV_DEC_*    0x11 XFER_DECISION     EB_TLV_ODEC_*   0xE1 OTA_DECISION
 *   EB_TLV_DBG_*    0xFF DEBUG             EB_TLV_OPROG_*  0xE4 OTA_PROGRESS
 *                                          EB_TLV_ODONE_*  0xE5 OTA_DONE
 *                                          EB_TLV_OFAIL_*  0xE6 OTA_FAIL
 *
 * The five 0xEn commands are the 8711 firmware update, and their TLV numbers are
 * deliberately IDENTICAL to those of the 0x1n command each one mirrors -- see the
 * 0xE0 block for the offset rule that makes that readable rather than accidental.
 */
#ifndef _EBADGE_CMD_H_
#define _EBADGE_CMD_H_

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Framing  (spec §2.3)
 *----------------------------------------------------------------------------*/
#define EBADGE_VER              0x01
#define EBADGE_FLAG_REQUEST     0x80    /* TLV_PARAMS_LENGTH marker           */
#define EBADGE_HDR_LEN          5       /* ver + cmd + 0x80 + len_lo + len_hi */
#define EBADGE_PARAMS_MAX       512     /* upper bound on params_len          */

/*----------------------------------------------------------------------------*
 *  Command IDs  (spec §3 command table)
 *----------------------------------------------------------------------------*/
/* Basic ops ----------------------------------------------------------- */
#define EB_CMD_SET_TIME         0x01    /* H->D  set RTC,        ack 0x04     */
#define EB_CMD_SEND_FILE        0x02    /* H->D  small BLE file, ack 0x04     */
#define EB_CMD_SEND_MSG         0x03    /* H->D  push message,   ack 0x04     */
#define EB_CMD_RESULT           0x04    /* D->H  generic result               */

/* JPEG stream preview over Wi-Fi  (V1.3 §4.5/§4.6) ------------------- */
#define EB_CMD_JPG_STREAM_OFFER 0x08    /* H->D  "start a preview stream"     */
#define EB_CMD_JPG_STREAM_DEC   0x09    /* D->H  reject/accept/negotiate      */

/* File transfer over Wi-Fi ------------------------------------------- */
#define EB_CMD_XFER_OFFER       0x10    /* H->D  "I want to send..."          */
#define EB_CMD_XFER_DECISION    0x11    /* D->H  reject/accept/timeout        */
#define EB_CMD_GET_AP_INFO      0x12    /* H->D  ask for SoftAP info          */
#define EB_CMD_AP_INFO          0x13    /* D->H  SoftAP creds + tcp port      */
#define EB_CMD_XFER_PROGRESS    0x14    /* D->H  recv/total bytes             */
#define EB_CMD_XFER_DONE        0x15    /* D->H  xfer succeeded               */
#define EB_CMD_XFER_FAIL        0x16    /* D->H  xfer failed w/ reason        */

/* Status queries ----------------------------------------------------- */
#define EB_CMD_GET_BATTERY      0x17    /* H->D  no params,      ack 0x18     */
#define EB_CMD_BATTERY          0x18    /* D->H  percent + charge state       */
#define EB_CMD_GET_STORAGE      0x19    /* H->D  no params,      ack 0x1A     */
#define EB_CMD_STORAGE_INFO     0x1A    /* D->H  capacity report              */

/* 0x05..0x07, 0x0A..0x0F and 0x1B..0x2F are RESERVED -- must not be used.
 * (V1.2 also reserved 0x08/0x09; V1.3 §3 allocates them to the stream pair
 * above, so the reserved ranges are now split around them.)              */

/* Wi-Fi module firmware update ---------------------------------------- */
/** Push new firmware to the 8711 over Wi-Fi, then over the SPI AT link.
 *
 *  NOT in PROT-001.  The 0xEn range was chosen for the same reason as 0xFF DEBUG:
 *  it sits outside every range the spec has allocated OR reserved (which stop at
 *  0x2F), so a future revision of the document cannot collide with it.
 *
 *  THE NUMBERS ARE THE FILE-TRANSFER NUMBERS PLUS 0xD0.  0xE0+n is the twin of
 *  0x10+n, and each twin carries the same TLVs with the same numbers:
 *
 *      0xE0 OTA_OFFER     <-> 0x10 XFER_OFFER
 *      0xE1 OTA_DECISION  <-> 0x11 XFER_DECISION
 *      (0xE2 / 0xE3)      <-> 0x12 GET_AP_INFO / 0x13 AP_INFO  -- NOT allocated
 *      0xE4 OTA_PROGRESS  <-> 0x14 XFER_PROGRESS
 *      0xE5 OTA_DONE      <-> 0x15 XFER_DONE
 *      0xE6 OTA_FAIL      <-> 0x16 XFER_FAIL
 *
 *  0xE2/0xE3 are left unallocated on purpose rather than by omission: the SoftAP
 *  credentials are a property of the radio, not of what is being sent over it, and
 *  all three sessions raise the same AP on the same port.  An update therefore
 *  answers with the SAME 0x13 AP_INFO a file transfer does -- one builder, one
 *  format, and an App needs no second parser for it.  Everything downstream of the
 *  credentials is per-session, and that is what gets its own number.
 *
 *  0x04 RESULT is NOT part of this flow.  It was, while 0xE0 was the only number
 *  available; now that the update has its own reject/verdict commands, using RESULT
 *  as well would give the same session two ways to end.                        */
#define EB_CMD_OTA_OFFER        0xE0    /* H->D  "I want to update the 8711"  */
#define EB_CMD_OTA_DECISION     0xE1    /* D->H  accept / reject the offer    */
#define EB_CMD_OTA_PROGRESS     0xE4    /* D->H  recv/total over the Wi-Fi leg*/
#define EB_CMD_OTA_DONE         0xE5    /* D->H  module took the image        */
#define EB_CMD_OTA_FAIL         0xE6    /* D->H  update failed w/ reason      */

/* Debug / bring-up ---------------------------------------------------- */
/** Vendor debug hook.  NOT in PROT-001 -- 0xFF is deliberately outside the
 *  spec's allocated and reserved ranges so it can never collide with a future
 *  revision.  Sub-function is selected by TLV, see the 0xFF block below.   */
#define EB_CMD_DEBUG            0xFF    /* H->D  debug subcmd,   ack 0x04     */

/*----------------------------------------------------------------------------*
 *  0x01 SET_TIME  (spec §4.1)
 *----------------------------------------------------------------------------*/
#define EB_TLV_DTIME_VALUE      0x01    /* 8B vs_date_time, see below         */

/** Byte layout of the 8-byte TLV_DATE_TIME value (spec §4.1). */
#define EB_DTIME_LEN            8
#define EB_DTIME_OFF_YEAR       0       /* u16 LE, 1582..9999                 */
#define EB_DTIME_OFF_MONTH      2       /* u8, 1..12                          */
#define EB_DTIME_OFF_DAY        3       /* u8, 1..31                          */
#define EB_DTIME_OFF_HOURS      4       /* u8, 0..23                          */
#define EB_DTIME_OFF_MINUTES    5       /* u8, 0..59                          */
#define EB_DTIME_OFF_SECONDS    6       /* u8, 0..59                          */
#define EB_DTIME_OFF_DOW        7       /* u8, 1=Mon .. 7=Sun                 */

/*----------------------------------------------------------------------------*
 *  0x02 SEND_FILE  (spec §4.2 -- compat path, not the wallpaper route)
 *----------------------------------------------------------------------------*/
#define EB_TLV_SFILE_NAME       0x01    /* utf-8, <=23B                       */
#define EB_TLV_SFILE_TYPE       0x02    /* 1B, see EB_FILE_TYPE_*             */
#define EB_TLV_SFILE_DATE       0x03    /* utf-8, <=23B                       */
#define EB_TLV_SFILE_LENGTH     0x04    /* 4B LE, body length                 */

/*----------------------------------------------------------------------------*
 *  0x03 SEND_MSG  (spec §4.3)
 *----------------------------------------------------------------------------*/
#define EB_TLV_MSG_APP_NAME     0x01    /* utf-8, <=23B                       */
#define EB_TLV_MSG_TITLE        0x02    /* utf-8, <=23B                       */
#define EB_TLV_MSG_TEXT         0x03    /* utf-8, <=23B                       */
#define EB_TLV_MSG_DATE         0x04    /* utf-8, <=23B                       */

/*----------------------------------------------------------------------------*
 *  0x04 RESULT  (spec §4.4)
 *----------------------------------------------------------------------------*/
#define EB_TLV_RESULT_CMD       0x01    /* 1B, the cmd id being answered      */
#define EB_TLV_RESULT_CODE      0x02    /* 1B, EB_RESULT_* (see errcode.h)    */

/*----------------------------------------------------------------------------*
 *  0x08 JPG_STREAM_OFFER  (spec §4.5, V1.3)
 *
 *  Same-screen preview: the App pushes a continuous run of JPEG frames over
 *  TCP instead of one stored file.  Nothing is written to storage, so there
 *  is no size / crc32 / replace_id here -- each frame carries its own length
 *  and CRC in the §6.2 header.  The device answers by itself (no dialog).
 *----------------------------------------------------------------------------*/
#define EB_TLV_SOFR_NAME        0x01    /* required, utf-8 <=23B              */
#define EB_TLV_SOFR_TYPE        0x02    /* required, EB_FILE_TYPE_* (not 0)   */
#define EB_TLV_SOFR_FPS         0x03    /* required, 1B frames per second     */

/*----------------------------------------------------------------------------*
 *  0x09 JPG_STREAM_DECISION  (spec §4.6, V1.3)
 *
 *  DECISION reuses the EB_DECISION_* values, but 0x02 means NEGOTIATE here
 *  (the device accepts at a different frame rate) where 0x11 uses it for
 *  TIMEOUT -- see EB_STREAM_DEC_NEGOTIATE in ebadge_errcode.h.
 *----------------------------------------------------------------------------*/
#define EB_TLV_SDEC_DECISION    0x01    /* required, EB_DECISION_*            */
#define EB_TLV_SDEC_REASON      0x02    /* optional, EB_XFER_ERR_* on reject  */
#define EB_TLV_SDEC_FPS         0x03    /* optional, 1B negotiated fps        */

/** Frame-rate window the device is willing to serve (§4.5 fixes no range,
 *  so these are ours: below MIN the preview is pointless, above MAX the
 *  decoder cannot keep up and we would just drop frames).                  */
#define EB_STREAM_FPS_MIN       1u
#define EB_STREAM_FPS_MAX       30u

/*----------------------------------------------------------------------------*
 *  0x10 XFER_OFFER  (spec §4.7)
 *----------------------------------------------------------------------------*/
#define EB_TLV_XFER_NAME        0x01    /* required, utf-8 <=23B              */
#define EB_TLV_XFER_TYPE        0x02    /* required, EB_FILE_TYPE_* (not 0)   */
#define EB_TLV_XFER_SIZE        0x03    /* required, 4B LE total length       */
#define EB_TLV_XFER_CRC32       0x04    /* required, 4B LE over whole body    */
#define EB_TLV_XFER_REPLACE_ID  0x05    /* optional, 2B LE file_id to replace */

/*----------------------------------------------------------------------------*
 *  0x11 XFER_DECISION  (spec §4.8)
 *----------------------------------------------------------------------------*/
#define EB_TLV_DEC_DECISION     0x01    /* required, EB_DECISION_*            */
#define EB_TLV_DEC_REASON       0x02    /* optional, EB_XFER_ERR_* on reject  */

/*----------------------------------------------------------------------------*
 *  0x13 AP_INFO  (spec §4.9) -- every TLV below is REQUIRED
 *----------------------------------------------------------------------------*/
#define EB_TLV_AP_SSID          0x01    /* utf-8 SSID, <=32B                  */
#define EB_TLV_AP_PASSWORD      0x02    /* utf-8, <=63B; len=0 if open        */
#define EB_TLV_AP_CHANNEL       0x03    /* 1B, 1..13 (2.4G)                   */
#define EB_TLV_AP_IPV4          0x04    /* 4B in NETWORK order: C0 A8 04 01   */
#define EB_TLV_AP_PORT          0x05    /* 2B LE, tcp listen port             */
#define EB_TLV_AP_PROTO         0x06    /* 1B, EB_AP_PROTO_RAW_TCP only       */
#define EB_TLV_AP_SECURITY      0x07    /* 1B, EB_AP_SEC_*                    */

/** 0x13 AP_INFO fixed values mandated by spec §4.9. */
#define EB_AP_PROTO_RAW_TCP     0x01    /* the only legal proto (V1.2/V1.3)   */
#define EB_AP_SEC_OPEN          0x00
#define EB_AP_SEC_WPA2_PSK      0x01
#define EB_AP_DEFAULT_IPV4_A    192     /* 192.168.4.1                        */
#define EB_AP_DEFAULT_IPV4_B    168
#define EB_AP_DEFAULT_IPV4_C    4
#define EB_AP_DEFAULT_IPV4_D    1
#define EB_AP_DEFAULT_PORT      9000    /* 0x2328, LE on the wire: 28 23      */

/*----------------------------------------------------------------------------*
 *  0x14 XFER_PROGRESS  (spec §4.10)
 *
 *  Note: the spec reports absolute byte counters, NOT a percentage.  The
 *  5% / 200ms throttle still uses a percentage internally to decide *when*
 *  to emit, but the payload carries recv/total.
 *
 *  Not emitted for a 0x08 preview stream: there is no total to report, and at
 *  24 fps a per-frame notify would swamp the BLE link.
 *----------------------------------------------------------------------------*/
#define EB_TLV_PROG_RECV        0x01    /* required, 4B LE bytes received     */
#define EB_TLV_PROG_TOTAL       0x02    /* required, 4B LE == offer size      */

/*----------------------------------------------------------------------------*
 *  0x15 XFER_DONE  (spec §4.11)
 *----------------------------------------------------------------------------*/
#define EB_TLV_DONE_FILE_ID     0x01    /* required, 2B LE, nonzero           */
#define EB_TLV_DONE_SIZE        0x02    /* required, 4B LE final size          */
#define EB_TLV_DONE_NAME        0x03    /* required, utf-8 final name on dev   */

/*----------------------------------------------------------------------------*
 *  0x16 XFER_FAIL  (spec §4.12) -- also used to report a stream abort
 *----------------------------------------------------------------------------*/
#define EB_TLV_FAIL_REASON      0x01    /* required, EB_XFER_ERR_*            */
#define EB_TLV_FAIL_DETAIL      0x02    /* optional, utf-8 <=23B debug text   */

/*----------------------------------------------------------------------------*
 *  0x18 BATTERY  (spec §4.13)
 *----------------------------------------------------------------------------*/
#define EB_TLV_BAT_PERCENT      0x01    /* required, 1B 0..100                */
#define EB_TLV_BAT_CHARGE       0x02    /* required, 1B EBADGE_BATT_*         */

/*----------------------------------------------------------------------------*
 *  0x1A STORAGE_INFO  (spec §4.14) -- every TLV below is REQUIRED
 *
 *  Widths matter here: TOTAL / FREE / WP_USED are uint64 LE and count BYTES
 *  (not KB).  FS_MARGIN is uint32 and must always answer EB_FS_MARGIN.
 *----------------------------------------------------------------------------*/
#define EB_TLV_STOR_TOTAL       0x01    /* 8B LE, partition total bytes       */
#define EB_TLV_STOR_FREE        0x02    /* 8B LE, currently free bytes        */
#define EB_TLV_STOR_WP_COUNT    0x03    /* 2B LE, wallpapers stored           */
#define EB_TLV_STOR_WP_USED     0x04    /* 8B LE, bytes used by wallpapers    */
#define EB_TLV_STOR_FS_MARGIN   0x05    /* 4B LE, always EB_FS_MARGIN         */

/** Filesystem metadata margin, spec §2.9 -- fixed at 4096 bytes. */
#define EB_FS_MARGIN            4096u

/*----------------------------------------------------------------------------*
 *  0xE0 OTA_OFFER  (not in PROT-001 -- 8711 firmware update)
 *
 *  §4.7's TLVs, same numbers, same widths.  An App builds this with its 0x10
 *  builder and changes the command byte:
 *
 *    NAME   a label for the image; echoed back in 0xE5 and required to match the
 *           EBXF header, exactly as a file name is.  Not a path and not stored --
 *           nothing on this device keeps the image after the module has taken it.
 *    TYPE   must be EB_FILE_TYPE_BIN.  The one field whose accepted VALUE differs
 *           from a file offer, and the check is worth having: an image offered as
 *           JPEG is a mis-built request, and refusing it costs nothing where
 *           accepting it means pushing a wallpaper at the radio's flash.
 *    SIZE   load bearing three times over -- AT+OTAUSER must declare the length to
 *           the module before the first byte is pushed, a raw image has no end
 *           marker so it is how the receive phase knows it is finished, and it is
 *           cross-checked against the EBXF header.
 *    CRC32  over the whole image, verified on the last received byte and BEFORE
 *           anything is pushed.  See ota_session.h for what this does and does not
 *           protect: it catches a corrupt or truncated image, and it cannot tell a
 *           wrong-but-valid image from the right one.
 *
 *  0x05 REPLACE_ID is not used: there is no file id to replace, the module has its
 *  own bank switching, and nothing here chooses a bank.  Held at its §4.7 meaning
 *  rather than reused, so an App that sends the full 0x10 TLV set has its extra
 *  field ignored instead of misread.  Unknown types must keep being IGNORED (which
 *  ebadge_tlv_get_* already does) so a newer App still gets a working update out of
 *  an older device rather than a parse failure.
 *----------------------------------------------------------------------------*/
#define EB_TLV_OTA_NAME         0x01    /* required, utf-8 <=23B -- as §4.7   */
#define EB_TLV_OTA_TYPE         0x02    /* required, must be EB_FILE_TYPE_BIN */
#define EB_TLV_OTA_SIZE         0x03    /* required, 4B LE image length       */
#define EB_TLV_OTA_CRC32        0x04    /* required, 4B LE over whole image   */
/* 0x05 is XFER_OFFER's REPLACE_ID: unused here, not free -- see above.       */

/*----------------------------------------------------------------------------*
 *  0xE1 OTA_DECISION  (the 0x11 twin)
 *
 *  Carries the ACCEPT that opens the Wi-Fi leg.  Reject and timeout are defined
 *  for symmetry with §4.8 and are reachable only if a confirmation prompt is ever
 *  reinstated -- as with 0x10, every reason to say no is known before the offer is
 *  accepted and goes out as 0xE6 with a §2.6 reason instead.
 *----------------------------------------------------------------------------*/
#define EB_TLV_ODEC_DECISION    0x01    /* required, EB_DECISION_*            */
#define EB_TLV_ODEC_REASON      0x02    /* optional, EB_XFER_ERR_* on reject  */

/*----------------------------------------------------------------------------*
 *  0xE4 OTA_PROGRESS  (the 0x14 twin)
 *
 *  Absolute byte counters over the WI-FI RECEIVE ONLY, throttled the same way
 *  §4.10 asks (200 ms or a 5% boundary).  The SPI push that follows reports no
 *  progress -- see the note in ota_session.h about the silence at 100%.
 *----------------------------------------------------------------------------*/
#define EB_TLV_OPROG_RECV       0x01    /* required, 4B LE bytes received     */
#define EB_TLV_OPROG_TOTAL      0x02    /* required, 4B LE == offer size      */

/*----------------------------------------------------------------------------*
 *  0xE5 OTA_DONE  (the 0x15 twin)
 *
 *  The module accepted the image and has been told to restart.  0x15's three TLVs
 *  are a file id, a size and a name; the id has no counterpart here, so 0x01 is
 *  LEFT OUT rather than given a new meaning -- SIZE and NAME keep the numbers they
 *  have in §4.11 so an App's 0x15 parser reads them correctly.
 *----------------------------------------------------------------------------*/
/* 0x01 is XFER_DONE's FILE_ID: no counterpart for an update, deliberately absent.*/
#define EB_TLV_ODONE_SIZE       0x02    /* required, 4B LE image bytes pushed */
#define EB_TLV_ODONE_NAME       0x03    /* required, utf-8, echoes the offer  */

/*----------------------------------------------------------------------------*
 *  0xE6 OTA_FAIL  (the 0x16 twin) -- byte-identical to §4.12
 *
 *  Every failure, before or after acceptance: a refused offer, a Wi-Fi leg that
 *  broke, a CRC mismatch, or a module that would not take the image.  The §2.6
 *  reason table was written for file transfer and has no OTA-specific codes, so the
 *  reason byte is chosen for what the App should DO and the exact cause travels in
 *  DETAIL -- which is where a reader should look.
 *----------------------------------------------------------------------------*/
#define EB_TLV_OFAIL_REASON     0x01    /* required, EB_XFER_ERR_*            */
#define EB_TLV_OFAIL_DETAIL     0x02    /* optional, utf-8 <=23B debug text   */

/*----------------------------------------------------------------------------*
 *  0xFF DEBUG  (not in PROT-001 -- vendor bring-up hook)
 *
 *  One required TLV selects the sub-function; the value TLV is a reserved
 *  placeholder so the wire format does not have to change when the first
 *  subcmd needs an argument.  Adding a subcmd means adding an EB_DBG_SUB_*
 *  value and a case in handlers/cmd_debug.c -- the frame shape stays fixed.
 *
 *  Minimal request (subcmd 0x01 = query the 8711's SoftAP info, no value):
 *
 *    01 FF 80 04 00   01 01 00 01
 *    |  |  |  |__|__ params_len = 4 (LE)
 *    |  |  |__ flags                  \__ TLV: type=0x01 len=0x0001 val=0x01
 *    |  |__ cmd 0xFF
 *    |__ ver 0x01
 *
 *  Answered by 0x04 RESULT.  Note V1.3 polarity: 0x00 == SUCCEED.
 *----------------------------------------------------------------------------*/
#define EB_TLV_DBG_SUBCMD       0x01    /* required, 1B, EB_DBG_SUB_*          */
#define EB_TLV_DBG_VALUE        0x02    /* optional, reserved subcmd argument  */

/** Length of the SUBCMD TLV value -- fixed at 1 byte. */
#define EB_DBG_SUBCMD_LEN       1

/** Upper bound on the reserved VALUE payload.  Kept small on purpose: this
 *  is a debug hook, not a data path -- anything bulky belongs on 0x02/0x10. */
#define EB_DBG_VALUE_MAX        32

/** Sub-function codes.  0x00 is left unassigned so a zero-filled or
 *  default-constructed request does not silently trigger a real action.
 *
 *  NOTE on 0x01: it started life as the no-op PING.  It is now the 8711 AP-info
 *  query, which is the check actually being used during bring-up; the plain
 *  liveness ping moved to 0x02.  Nothing had shipped against the old numbering.
 *
 *  The two Wi-Fi subcmds answer RESULT as soon as the AT command is QUEUED --
 *  not when the 8711 replies.  We drive the SPI link, so the round trip is ours
 *  to start, but AT+WLSTATE still costs one and AT+WLSTARTAP does not print OK
 *  until the radio is on the air (its handler polls its own driver at ~1 Hz for
 *  up to 20 iterations).  Waiting for either inside a BLE handler would stall
 *  l2_task -- the single serialiser for every BLE command -- for seconds or tens
 *  of seconds.  So RESULT here means "accepted for submission", not "answered";
 *  the reply is printed to the device log by at_wifi.c when it lands.          */
#define EB_DBG_SUB_NONE         0x00    /* invalid -- rejected                 */
#define EB_DBG_SUB_WIFI_AP_INFO 0x01    /* 8711 AT+WLSTATE, reply -> log       */
#define EB_DBG_SUB_PING         0x02    /* no-op liveness check, acks SUCCEED  */
#define EB_DBG_SUB_WIFI_START_AP 0x03   /* 8711 AT+WLSTARTAP, reply -> log     */

/** 0x04 / 0x05 -- the two transfer-control actions, driven by hand.
 *
 *  These numbers have been re-pointed twice.  They began as a reserved Wi-Fi data
 *  tunnel (AT+WLRECV / AT+WLSEND) that no 8711 firmware ever implemented, then
 *  became AT+XFERSTOP / AT+XFERACK -- commands the in-house firmware provided,
 *  which built the EBXR itself out of a status and a reason we handed it.  The
 *  standard command set has neither: the 8711 is a network card, and the EBXR is
 *  eight bytes WE pack and push over the phone's own TCP connection with
 *  AT+SKTSENDRAW.  Nothing ever shipped against either older meaning.
 *
 *  So both subcmds now act on ebadge_port_tcp, which is the module that owns
 *  those bytes.  The numbers were kept because the two ACTIONS survived the
 *  firmware change even though the commands behind them did not.
 *
 *  0x04 STOP takes no argument: ebadge_port_tcp_abort() cuts the phone's
 *  connection with no EBXR -- the same thing xfer_session does when an EBXF
 *  header contradicts the BLE offer.  On the wire it is AT+SKTDEL.
 *
 *  0x05 ACK is the one subcmd that uses the VALUE TLV: byte 0 is status
 *  (0 = fail, 1 = success, the EBXR's own polarity, which is the OPPOSITE of the
 *  V1.3 sec.2.5 BLE result codes) and byte 1, if present, is the eBadge sec.2.6
 *  reason code.  An absent or empty VALUE is refused rather than defaulted --
 *  both 0 and 1 are meaningful, so guessing would send a real phone a verdict
 *  nobody asked for.  Which is worth knowing before typing one: used mid-upload,
 *  0x05 ends that upload with the code typed here.
 *
 *  Both answer NOT_READY -- immediately, not seconds later -- when no phone is
 *  connected, because then there is no seed link to write to.  NOT_READY rather
 *  than FAILED: nothing is broken, there is simply no connection, which is the
 *  normal outcome of poking these with no upload in flight.  They exist for a
 *  human watching the console during bring-up; the firmware itself reaches
 *  ebadge_port_tcp directly from xfer_session.                                 */
#define EB_DBG_SUB_XFER_STOP    0x04    /* port_tcp abort, no argument         */
#define EB_DBG_SUB_XFER_ACK     0x05    /* port_tcp ack, VALUE=<status>[,<rsn>]*/

/*----------------------------------------------------------------------------*
 *  File type enum  (spec §2.7) -- shared by SEND_FILE, XFER_OFFER,
 *  JPG_STREAM_OFFER, and both TCP headers (EBXF §5.2 / EBXS §6.2)
 *----------------------------------------------------------------------------*/
#define EB_FILE_TYPE_UNKNOWN    0x00    /* illegal in an OFFER                */
#define EB_FILE_TYPE_JPEG       0x01
#define EB_FILE_TYPE_PNG        0x02
#define EB_FILE_TYPE_GIF        0x03
#define EB_FILE_TYPE_JPEG_STREAM 0x04
#define EB_FILE_TYPE_VIDEO      0x05
#define EB_FILE_TYPE_BIN        0x06
#define EB_FILE_TYPE_TEXT       0x07
#define EB_FILE_TYPE_MAX        EB_FILE_TYPE_TEXT

/*----------------------------------------------------------------------------*
 *  Length caps  (spec §2.8)
 *----------------------------------------------------------------------------*/
#define EB_MAX_FILE_NAME        23      /* utf-8 bytes, excluding NUL         */
#define EB_MAX_DATE_STR         23
#define EB_MAX_MSG_STR          23      /* title / text / app name / date     */
#define EB_MAX_AP_SSID          32
#define EB_MAX_AP_PASSWORD      63

/**
 * Sanity cap on a 0x02 SEND_FILE body.  The spec does not fix a number here
 * (§8.3 leaves the single-file limit open, suggesting ~2 MiB for the Wi-Fi
 * path), but §4.2 and §7's "大图禁止 BLE 0x02 主路径" restrict this BLE route
 * to debug and small config files -- wallpaper must go through 0x10 + TCP.
 * 64 KiB is generous for that role while keeping a bogus length from parking
 * the RX stream in raw mode for minutes.  Bodies are streamed, never
 * buffered, so this is policy only.
 */
#define EB_MAX_SEND_FILE_BYTES  (64u * 1024u)

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_CMD_H_ */
