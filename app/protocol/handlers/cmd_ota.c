/**
 * @file    cmd_ota.c
 * @brief   0xE0 OTA_OFFER -- parse TLVs, delegate to ota_session_offer().
 *
 * One TLV is read: 0x03 size, u32 LE.  That number is not this command's own
 * invention -- it is where §4.7 puts a file length, and the App fills this offer in
 * the same shape as a file offer, so the two agree by construction rather than by
 * coincidence.  See the 0xE0 block in ebadge_cmd.h for why the rest of the low
 * numbers are held at their XFER meanings instead of being reused.
 *
 * Unknown TLVs are IGNORED rather than refused, and that is what makes reusing the
 * shape work: ebadge_tlv_get_u32() picks out the type it wants and does not care
 * what else came along, so an offer that also carries a name, a type and a CRC --
 * or a version this device has never heard of -- still yields an update instead of
 * a parse failure.  Nothing here validates the image; that is the deliberate
 * arrangement documented in ota_session.h.
 *
 * A missing size TLV is passed through as 0 rather than answered here.  The
 * session refuses it -- 0x04 RESULT(FAILED) -- and every other refusal reason
 * lives there too, so there is exactly one place that decides what an offer is
 * answered with.
 */
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_log.h"
#include "../wifi_ota/ota_session.h"

void handle_ota_offer(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    uint32_t size = 0;
    bool has_size = ebadge_tlv_get_u32(tlvs, n_tlv, EB_TLV_OTA_SIZE, &size);

    EBADGE_LOG2("OTA_OFFER: size=%u (present=%d)", size, (int)has_size);
    if (!has_size)
    {
        EBADGE_WARN("OTA_OFFER: no size TLV -- the session will refuse it");
    }

    EBADGE_LOG("OTA_OFFER: -> ota_session_offer (reply 0x13 or 0x04 via state machine)");
    ota_session_offer(size);
}
