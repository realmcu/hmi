/**
 * @file    ebxf_frame.h
 * @brief   EBXF header + EBXR ack -- carried over the Wi-Fi TCP data plane.
 *
 * Wire format per eBadge-PROT-001 V1.2 §5.2 / §5.3.  Multibyte fields LE.
 *
 * EBXF upload header -- 40 bytes, then `size` bytes of payload:
 *
 *   off  len  field
 *    0    4   magic     'E' 'B' 'X' 'F'  (45 42 58 46)
 *    4    1   version   0x01
 *    5    1   file_type EB_FILE_TYPE_* (spec §2.7)
 *    6    1   name_len  valid bytes in `name`, 0..23
 *    7    1   reserved  0x00
 *    8    4   size      uint32 LE, payload length; == offer TLV_XFER_SIZE
 *   12    4   crc32     uint32 LE over payload; == offer TLV_XFER_CRC32
 *   16   24   name      utf-8, first name_len valid, rest 0x00
 *   40  size  payload   raw file bytes
 *
 * EBXR ack -- 8 bytes, written back on the same connection after receipt:
 *
 *   off  len  field
 *    0    4   magic     'E' 'B' 'X' 'R'  (45 42 58 52)
 *    4    1   status    0x00 = failure, 0x01 = success
 *    5    1   reason    EB_XFER_ERR_* on failure, 0x00 on success
 *    6    2   reserved  0x0000
 *
 * Note the status polarity: 0 is FAILURE here, same as the generic §2.5
 * result code.  A TCP-level success does NOT replace BLE 0x15 DONE -- the
 * App treats 0x15/0x16 as authoritative (spec §5.3).
 */
#ifndef _EBADGE_EBXF_FRAME_H_
#define _EBADGE_EBXF_FRAME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EBXF_HDR_LEN       40
#define EBXF_VER           0x01    /* the only version V1.2 defines          */
#define EBXF_NAME_LEN      24      /* on-wire name field width               */
#define EBXR_LEN            8

/** EBXR status byte (spec §5.3) -- note 0 means failure. */
#define EBXR_STATUS_FAILED  0x00
#define EBXR_STATUS_OK      0x01

/*----------------------------------------------------------------------------*
 *  Decoded view (host order, populated by ebxf_hdr_parse)
 *----------------------------------------------------------------------------*/
typedef struct
{
    uint8_t  ver;
    uint8_t  file_type;
    uint8_t  name_len;                      /* as sent, 0..EBXF_NAME_LEN     */
    uint32_t file_size;
    uint32_t crc32;
    char     file_name[EBXF_NAME_LEN + 1];  /* +1 for guaranteed NUL         */
} ebxf_hdr_t;

/**
 * @brief  Parse a 40-byte EBXF header buffer into @p out.
 * @return 0 on success, negative if magic or version are wrong.
 */
int  ebxf_hdr_parse(const uint8_t *buf, ebxf_hdr_t *out);

/**
 * @brief  Serialise an 8-byte EBXR ack.
 * @param  status  EBXR_STATUS_OK or EBXR_STATUS_FAILED.
 * @param  reason  EB_XFER_ERR_* when failing; pass 0 on success.
 */
void ebxr_pack(uint8_t out[EBXR_LEN], uint8_t status, uint8_t reason);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_EBXF_FRAME_H_ */
