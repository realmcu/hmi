/**
 * @file    cmd_result.c
 * @brief   0x04 RESULT (App -> Dev direction) -- the App acking a
 *          device-initiated notify.  Currently log-only; no reply, since
 *          acking an ack would loop.
 *
 * Spec §4.4 TLVs: 0x01 = the cmd being answered, 0x02 = EB_RESULT_*.
 * V1.3 §2.5 widened that code to four values AND inverted 0x00/0x01, so the
 * decode below must be a real lookup -- a "code ? failed : ok" test would
 * report every App success as a failure.
 */
#include <stdint.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"

static const char *result_code_str(uint8_t code)
{
    switch (code)
    {
    case EB_RESULT_SUCCEED:   return "SUCCEED";
    case EB_RESULT_FAILED:    return "FAILED";
    case EB_RESULT_NOT_READY: return "NOT_READY";
    case EB_RESULT_BUSY:      return "BUSY";
    default:                  return "?";
    }
}

void handle_result(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    uint8_t cmd_ref = 0xFF, code = 0xFF;
    bool    has_ref  = ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_RESULT_CMD,  &cmd_ref);
    bool    has_code = ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_RESULT_CODE, &code);

    if (!has_ref || !has_code)
    {
        EBADGE_WARN2("RESULT<-App: incomplete (ref=%d code=%d present)",
                     (int)has_ref, (int)has_code);
        return;
    }

    EBADGE_LOG2("RESULT<-App: cmd=0x%02x %s (ack-only, no reply)",
                cmd_ref, result_code_str(code));

    /* TODO(app): correlate with the outstanding request for `cmd_ref`.  There
     * is no request table yet -- the device never retries, so nothing depends
     * on this today.                                                        */
}
