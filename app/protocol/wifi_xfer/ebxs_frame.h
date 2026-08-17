/**
 * @file    ebxs_frame.h
 * @brief   EBXS header -- one JPEG preview frame on the Wi-Fi TCP data plane.
 *
 * Wire format per eBadge-PROT-001 V1.3 §6.2.  Multibyte fields LE.
 *
 * EBXS frame header -- 14 bytes, then `size` bytes of JPEG payload:
 *
 *   off  len  field
 *    0    4   magic     'E' 'B' 'X' 'F'  (45 42 58 46)   <- same as EBXF!
 *    4    1   version   0x01
 *    5    1   file_type EB_FILE_TYPE_JPEG_STREAM (spec §2.7)
 *    6    4   size      uint32 LE, THIS frame's payload length
 *   10    4   crc32     uint32 LE over THIS frame's payload
 *   14  size  payload   raw JPEG bytes
 *
 * Two things make this NOT a shortened EBXF (§5.2), despite the shared magic:
 *
 *   1) The header repeats for every frame; a session is a continuous run of
 *      (header, payload) pairs on one connection, not one header up front.
 *   2) `size` / `crc32` are per-FRAME.  There is no session total, no name,
 *      and nothing is cross-checked against the BLE offer -- 0x08 carries no
 *      size or crc to check against.
 *
 * Because the magic is identical, the receiver cannot sniff which layout is
 * on the wire.  The session type is decided by the BLE offer that opened it
 * (0x10 -> EBXF/40B, 0x08 -> EBXS/14B) and the demux is by session, not by
 * inspection.  Do not "auto-detect" here.
 *
 * The §6.3 ack frame is the same 8-byte EBXR as §5.3 and is OPTIONAL for
 * streams; see ebxf_frame.h for its codec.
 */
#ifndef _EBADGE_EBXS_FRAME_H_
#define _EBADGE_EBXS_FRAME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EBXS_HDR_LEN       14
#define EBXS_VER           0x01    /* the only version V1.3 defines           */

/**
 * Sanity cap on one preview frame.  §8.3 leaves the image limit open; a
 * 466x466 JPEG frame is a few tens of KB, so 512 KiB is far above anything
 * legitimate while still catching a corrupt length field before we start
 * counting down a bogus payload and stall the session until the 3s frame
 * timeout fires.
 */
#define EBXS_MAX_FRAME_BYTES  (512u * 1024u)

/*----------------------------------------------------------------------------*
 *  Decoded view (host order, populated by ebxs_hdr_parse)
 *----------------------------------------------------------------------------*/
typedef struct
{
    uint8_t  ver;
    uint8_t  file_type;
    uint32_t frame_size;                    /* payload bytes that follow     */
    uint32_t crc32;                         /* over those payload bytes      */
} ebxs_hdr_t;

/**
 * @brief  Parse a 14-byte EBXS frame header into @p out.
 *
 * Validates magic, version, and that frame_size is in (0, EBXS_MAX_FRAME_BYTES].
 * file_type is copied out but NOT judged here -- the session owns that policy.
 *
 * @return 0 on success; negative on bad magic (-2), version (-3) or size (-4).
 */
int ebxs_hdr_parse(const uint8_t *buf, ebxs_hdr_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_EBXS_FRAME_H_ */
