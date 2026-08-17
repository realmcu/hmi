/**
 * @file    cmd_jpg_stream_offer.c
 * @brief   0x08 JPG_STREAM_OFFER -- parse TLVs, delegate to stream_session.
 *
 * Spec §4.5 (V1.3) TLVs, all three required:
 *   0x01 name (utf-8 <=23B)   0x02 file_type (must be 0x04 JPEG_STREAM)
 *   0x03 fps  (1B)
 *
 * The TLV numbers restart at 0x01 per command (§2.4), so these are the
 * EB_TLV_SOFR_* set -- NOT the EB_TLV_XFER_* set used by 0x10, even though
 * the first two mean the same thing.
 *
 * Note the §4.5 hex example in the spec is wrong twice over (its params_len
 * does not match its body, and it omits the required NAME TLV), and both
 * §4.6 examples show cmd byte 0x11 instead of 0x09.  This handler follows the
 * TLV tables, which are self-consistent.
 *
 * Like 0x10, a malformed offer is still fed to the state machine so the reply
 * (0x09 STREAM_DECISION) is emitted from exactly one place.
 */
#include <string.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../wifi_xfer/stream_session.h"

void handle_jpg_stream_offer(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    const ebadge_tlv_t *tname = ebadge_tlv_find(tlvs, n_tlv, EB_TLV_SOFR_NAME);
    uint8_t ftype = EB_FILE_TYPE_UNKNOWN;
    uint8_t fps   = 0;

    bool has_type = ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_SOFR_TYPE, &ftype);
    bool has_fps  = ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_SOFR_FPS,  &fps);

    char name[EB_MAX_FILE_NAME + 1] = {0};
    if (tname && tname->len)
    {
        uint16_t n = (tname->len < sizeof(name) - 1) ? tname->len
                     : (uint16_t)(sizeof(name) - 1);
        memcpy(name, tname->val, n);
    }

    EBADGE_LOG3("STREAM_OFFER: name=\"%s\" type=%d fps=%d",
                name, (int)ftype, (int)fps);

    if (!tname || tname->len == 0 || !has_type || !has_fps)
    {
        EBADGE_WARN2("STREAM_OFFER: missing required TLV (type=%d fps=%d present)",
                     (int)has_type, (int)has_fps);
        /* Fall through: file_type UNKNOWN gets FMT_UNSUPPORTED, fps 0 gets
         * negotiated up to our preferred rate -- both decided by the state
         * machine, which owns the 0x09 reply.                              */
    }

    EBADGE_LOG("STREAM_OFFER: -> stream_session_offer (reply 0x09 via state machine)");
    stream_session_offer(name, ftype, fps);
}
