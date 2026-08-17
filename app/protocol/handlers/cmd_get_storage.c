/**
 * @file    cmd_get_storage.c
 * @brief   0x19 GET_STORAGE -- respond with 0x1A STORAGE_INFO.
 *
 * Spec §4.14 TLVs, all five required:
 *   0x01 total     8B LE bytes
 *   0x02 free      8B LE bytes
 *   0x03 wp_count  2B LE
 *   0x04 wp_used   8B LE bytes
 *   0x05 fs_margin 4B LE, always EB_FS_MARGIN (§2.9)
 *
 * Params size: 3*(3+8) + (3+2) + (3+4) = 45 bytes.
 */
#include <stdint.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../port/ebadge_port_storage.h"

void handle_get_storage(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    (void)tlvs; (void)n_tlv;

    ebadge_storage_stat_t st = {0};
    if (ebadge_port_storage_stat(&st) != 0)
    {
        EBADGE_WARN("GET_STORAGE: port_storage stat FAIL -> RESULT FAILED");
        (void)ebadge_l2_result_send(EB_CMD_GET_STORAGE, EB_RESULT_FAILED);
        return;
    }

    /* Log in KiB to keep the line readable; the wire carries raw bytes. */
    EBADGE_LOG3("GET_STORAGE: total=%uKiB free=%uKiB wp_used=%uKiB",
                (unsigned)(st.total_bytes   >> 10),
                (unsigned)(st.free_bytes    >> 10),
                (unsigned)(st.wp_used_bytes >> 10));
    EBADGE_LOG2("GET_STORAGE: wp_count=%d fs_margin=%u  (port_storage STUB)",
                (int)st.wp_count, (unsigned)EB_FS_MARGIN);

    uint8_t  params[64];
    uint16_t off = 0;
    ebadge_tlv_put_u64(params, sizeof(params), &off,
                       EB_TLV_STOR_TOTAL,     st.total_bytes);
    ebadge_tlv_put_u64(params, sizeof(params), &off,
                       EB_TLV_STOR_FREE,      st.free_bytes);
    ebadge_tlv_put_u16(params, sizeof(params), &off,
                       EB_TLV_STOR_WP_COUNT,  st.wp_count);
    ebadge_tlv_put_u64(params, sizeof(params), &off,
                       EB_TLV_STOR_WP_USED,   st.wp_used_bytes);
    ebadge_tlv_put_u32(params, sizeof(params), &off,
                       EB_TLV_STOR_FS_MARGIN, EB_FS_MARGIN);
    (void)ebadge_l2_notify_send(EB_CMD_STORAGE_INFO, params, off);
}
