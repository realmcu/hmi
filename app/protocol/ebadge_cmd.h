/**
 * @file    ebadge_cmd.h
 * @brief   eBadge Protocol V1.2 command IDs and TLV type codes.
 *
 * Wire spec:  eBadge-PROT-001 V1.2 (2026-08-11).  Frame header is fixed 5B,
 * LE byte order, no CRC / no ACK; reliability is ATT-native.
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
 *   EB_TLV_DTIME_*  0x01 SET_TIME          EB_TLV_AP_*    0x13 AP_INFO
 *   EB_TLV_SFILE_*  0x02 SEND_FILE         EB_TLV_PROG_*  0x14 PROGRESS
 *   EB_TLV_MSG_*    0x03 SEND_MSG          EB_TLV_DONE_*  0x15 XFER_DONE
 *   EB_TLV_RESULT_* 0x04 RESULT            EB_TLV_FAIL_*  0x16 XFER_FAIL
 *   EB_TLV_XFER_*   0x10 XFER_OFFER        EB_TLV_BAT_*   0x18 BATTERY
 *   EB_TLV_DEC_*    0x11 XFER_DECISION     EB_TLV_STOR_*  0x1A STORAGE_INFO
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

/* 0x05..0x0F and 0x1B..0x2F are RESERVED -- must not be used (spec §3). */

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
 *  0x10 XFER_OFFER  (spec §4.5)
 *----------------------------------------------------------------------------*/
#define EB_TLV_XFER_NAME        0x01    /* required, utf-8 <=23B              */
#define EB_TLV_XFER_TYPE        0x02    /* required, EB_FILE_TYPE_* (not 0)   */
#define EB_TLV_XFER_SIZE        0x03    /* required, 4B LE total length       */
#define EB_TLV_XFER_CRC32       0x04    /* required, 4B LE over whole body    */
#define EB_TLV_XFER_REPLACE_ID  0x05    /* optional, 2B LE file_id to replace */

/*----------------------------------------------------------------------------*
 *  0x11 XFER_DECISION  (spec §4.6)
 *----------------------------------------------------------------------------*/
#define EB_TLV_DEC_DECISION     0x01    /* required, EB_DECISION_*            */
#define EB_TLV_DEC_REASON       0x02    /* optional, EB_XFER_ERR_* on reject  */

/*----------------------------------------------------------------------------*
 *  0x13 AP_INFO  (spec §4.7) -- every TLV below is REQUIRED
 *----------------------------------------------------------------------------*/
#define EB_TLV_AP_SSID          0x01    /* utf-8 SSID, <=32B                  */
#define EB_TLV_AP_PASSWORD      0x02    /* utf-8, <=63B; len=0 if open        */
#define EB_TLV_AP_CHANNEL       0x03    /* 1B, 1..13 (2.4G)                   */
#define EB_TLV_AP_IPV4          0x04    /* 4B in NETWORK order: C0 A8 04 01   */
#define EB_TLV_AP_PORT          0x05    /* 2B LE, tcp listen port             */
#define EB_TLV_AP_PROTO         0x06    /* 1B, EB_AP_PROTO_RAW_TCP only       */
#define EB_TLV_AP_SECURITY      0x07    /* 1B, EB_AP_SEC_*                    */

/** 0x13 AP_INFO fixed values mandated by spec §4.7. */
#define EB_AP_PROTO_RAW_TCP     0x01    /* the only legal proto in V1.2       */
#define EB_AP_SEC_OPEN          0x00
#define EB_AP_SEC_WPA2_PSK      0x01
#define EB_AP_DEFAULT_IPV4_A    192     /* 192.168.4.1                        */
#define EB_AP_DEFAULT_IPV4_B    168
#define EB_AP_DEFAULT_IPV4_C    4
#define EB_AP_DEFAULT_IPV4_D    1
#define EB_AP_DEFAULT_PORT      9000    /* 0x2328, LE on the wire: 28 23      */

/*----------------------------------------------------------------------------*
 *  0x14 XFER_PROGRESS  (spec §4.8)
 *
 *  Note: the spec reports absolute byte counters, NOT a percentage.  The
 *  5% / 200ms throttle still uses a percentage internally to decide *when*
 *  to emit, but the payload carries recv/total.
 *----------------------------------------------------------------------------*/
#define EB_TLV_PROG_RECV        0x01    /* required, 4B LE bytes received     */
#define EB_TLV_PROG_TOTAL       0x02    /* required, 4B LE == offer size      */

/*----------------------------------------------------------------------------*
 *  0x15 XFER_DONE  (spec §4.9)
 *----------------------------------------------------------------------------*/
#define EB_TLV_DONE_FILE_ID     0x01    /* required, 2B LE, nonzero           */
#define EB_TLV_DONE_SIZE        0x02    /* required, 4B LE final size          */
#define EB_TLV_DONE_NAME        0x03    /* required, utf-8 final name on dev   */

/*----------------------------------------------------------------------------*
 *  0x16 XFER_FAIL  (spec §4.10)
 *----------------------------------------------------------------------------*/
#define EB_TLV_FAIL_REASON      0x01    /* required, EB_XFER_ERR_*            */
#define EB_TLV_FAIL_DETAIL      0x02    /* optional, utf-8 <=23B debug text   */

/*----------------------------------------------------------------------------*
 *  0x18 BATTERY  (spec §4.11)
 *----------------------------------------------------------------------------*/
#define EB_TLV_BAT_PERCENT      0x01    /* required, 1B 0..100                */
#define EB_TLV_BAT_CHARGE       0x02    /* required, 1B EBADGE_BATT_*         */

/*----------------------------------------------------------------------------*
 *  0x1A STORAGE_INFO  (spec §4.12) -- every TLV below is REQUIRED
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
 *  File type enum  (spec §2.7) -- shared by SEND_FILE, XFER_OFFER, EBXF hdr
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
 * (§7.3 leaves the single-file limit open, suggesting ~2 MiB for the Wi-Fi
 * path), but §4.2/§7.5 restrict this BLE route to debug and small config
 * files -- wallpaper must go through 0x10 + TCP.  64 KiB is generous for that
 * role while keeping a bogus length from parking the RX stream in raw mode
 * for minutes.  Bodies are streamed, never buffered, so this is policy only.
 */
#define EB_MAX_SEND_FILE_BYTES  (64u * 1024u)

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_CMD_H_ */
