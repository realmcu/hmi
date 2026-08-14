/**
 * @file    cmd_xfer_offer.c
 * @brief   0x10 XFER_OFFER -- parse TLVs, delegate to xfer_session_offer().
 *
 * Spec §4.5 TLVs (all required except REPLACE_ID):
 *   0x01 name (utf-8 <=23B)  0x02 file_type  0x03 size u32
 *   0x04 crc32 u32           0x05 replace_id u16 (optional)
 *
 * A missing required TLV is answered by xfer_session with 0x16 XFER_FAIL,
 * not 0x04 RESULT -- the offer lives entirely in the transfer state machine.
 */
#include <string.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../wifi_xfer/xfer_session.h"

void handle_xfer_offer(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    const ebadge_tlv_t *tname = ebadge_tlv_find(tlvs, n_tlv, EB_TLV_XFER_NAME);
    uint8_t   ftype = EB_FILE_TYPE_UNKNOWN;
    uint32_t  fsize = 0;
    uint32_t  crc32 = 0;
    uint16_t  repl  = 0;

    bool has_type = ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_XFER_TYPE,  &ftype);
    bool has_size = ebadge_tlv_get_u32(tlvs, n_tlv, EB_TLV_XFER_SIZE,  &fsize);
    bool has_crc  = ebadge_tlv_get_u32(tlvs, n_tlv, EB_TLV_XFER_CRC32, &crc32);
    /* REPLACE_ID is optional -- absent means "store as a new file". */
    (void)ebadge_tlv_get_u16(tlvs, n_tlv, EB_TLV_XFER_REPLACE_ID, &repl);

    /* Truncate the name to the §2.8 cap rather than the on-wire field width,
     * so an over-long name is cut where the spec says it ends.             */
    char name[EB_MAX_FILE_NAME + 1] = {0};
    if (tname && tname->len)
    {
        uint16_t n = (tname->len < sizeof(name) - 1) ? tname->len
                     : (uint16_t)(sizeof(name) - 1);
        memcpy(name, tname->val, n);
    }

    EBADGE_LOG3("XFER_OFFER: type=%d size=%u repl=%d",
                (int)ftype, fsize, (int)repl);
    EBADGE_LOG2("XFER_OFFER: name=\"%s\" crc32=0x%08x", name, crc32);

    if (!tname || tname->len == 0 || !has_type || !has_size || !has_crc ||
        fsize == 0)
    {
        EBADGE_WARN2("XFER_OFFER: missing required TLV (type=%d size=%d present)",
                     (int)has_type, (int)has_size);
        /* Feed it through anyway with file_type UNKNOWN / size 0 so the
         * state machine owns the reply -- it answers 0x16 FAIL with the
         * right §2.6 reason and keeps the session in one place.            */
    }

    EBADGE_LOG("XFER_OFFER: -> xfer_session_offer (reply 0x11 DECISION via state machine)");
    xfer_session_offer(name, ftype, fsize, crc32, repl);
}
