/**
 * @file    cmd_ota.c
 * @brief   0xE0 OTA_OFFER -- parse TLVs, delegate to ota_session_offer().
 *
 * A copy of cmd_xfer_offer.c with one TLV fewer, and that is the point: the TLV
 * numbers ARE §4.7's, so an App builds this with its 0x10 builder and changes the
 * command byte.  See the 0xE0 block in ebadge_cmd.h for the offset rule.
 *
 *   0x01 name (utf-8 <=23B)  0x02 type (must be BIN)  0x03 size u32
 *   0x04 crc32 u32           0x05 replace_id -- not used by an update
 *
 * Unknown TLVs are IGNORED rather than refused, which is what makes reusing the
 * shape work: ebadge_tlv_get_*() picks out the types it wants and does not care
 * what else came along, so an offer that also carries a replace_id -- or a version
 * field this device has never heard of -- still yields an update instead of a parse
 * failure.
 *
 * A missing required TLV is passed through as 0 / UNKNOWN rather than answered
 * here.  The session refuses it with 0xE6 and a §2.6 reason, and every other
 * refusal reason lives there too, so there is exactly one place that decides what
 * an offer is answered with.
 */
#include <string.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../wifi_ota/ota_session.h"

void handle_ota_offer(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    const ebadge_tlv_t *tname = ebadge_tlv_find(tlvs, n_tlv, EB_TLV_OTA_NAME);
    uint8_t   ftype = EB_FILE_TYPE_UNKNOWN;
    uint32_t  fsize = 0;
    uint32_t  crc32 = 0;

    bool has_type = ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_OTA_TYPE,  &ftype);
    bool has_size = ebadge_tlv_get_u32(tlvs, n_tlv, EB_TLV_OTA_SIZE,  &fsize);
    bool has_crc  = ebadge_tlv_get_u32(tlvs, n_tlv, EB_TLV_OTA_CRC32, &crc32);

    /* Truncate to the §2.8 cap rather than the on-wire field width, so an
     * over-long name is cut where the spec says it ends. */
    char name[EB_MAX_FILE_NAME + 1] = {0};
    if (tname != NULL && tname->len != 0U)
    {
        uint16_t n = (tname->len < sizeof(name) - 1U) ? tname->len
                     : (uint16_t)(sizeof(name) - 1U);
        memcpy(name, tname->val, n);
    }

    EBADGE_LOG2("OTA_OFFER: type=%d size=%u", (int)ftype, fsize);
    EBADGE_LOG2("OTA_OFFER: name=\"%s\" crc32=0x%08x", name, crc32);

    if (!has_type || !has_size || !has_crc)
    {
        /* Type and size absent are refused at the offer: they land on UNKNOWN (not
         * BIN) and 0, which ota_session_offer() checks.  A missing crc32 becomes 0
         * and gets one step further -- the EBXF cross-check refuses it once the
         * 40-byte header restates the real value, so 40 bytes rather than an upload.
         * Only a sender that puts 0 in BOTH planes reaches the end, and then the
         * running CRC is what refuses it. */
        EBADGE_LOG3("OTA_OFFER: missing required TLV (type=%d size=%d crc=%d "
                    "present) -- the session will refuse it",
                    (int)has_type, (int)has_size, (int)has_crc);
    }
    else if (tname == NULL || tname->len == 0U)
    {
        /* Tolerated, unlike the three above: the name is a label, not something
         * this device stores or acts on, so an absent one only means the EBXF
         * header's name is not cross-checked.  Same latitude the file path gives. */
        EBADGE_LOG("OTA_OFFER: no name TLV -- the EBXF name will not be checked");
    }

    EBADGE_LOG("OTA_OFFER: -> ota_session_offer (reply 0xE1+0x13, or 0xE6)");
    ota_session_offer(name, ftype, fsize, crc32);
}
