/**
 * @file    cmd_get_ap_info.c
 * @brief   0x12 GET_AP_INFO -- App re-queries the SoftAP info after losing it
 *          (app restart, notify missed).  If a transfer is between ACCEPT and
 *          DONE we should re-emit the same 0x13 AP_INFO; with no AP up there
 *          is nothing to report, so we answer 0x04 RESULT(FAILED).
 *
 * Spec §2.5 has exactly two result codes, so "no AP active" and "internal
 * error" collapse to the same FAILED on the wire -- the distinction lives in
 * the log only.
 */
#include <stdint.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"

void handle_get_ap_info(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    (void)tlvs; (void)n_tlv;
    EBADGE_LOG("GET_AP_INFO: no AP context -> RESULT FAILED  (TODO: re-emit 0x13 when xfer AP active)");
    /* TODO(app): if xfer_session has an active AP (WAIT_STA or RECV),
     * re-emit 0x13 with the current creds.  Requires xfer_session to keep
     * and expose the ebadge_softap_info_t it handed to port_softap; it
     * currently discards it after emit_ap_info().                         */
    (void)ebadge_l2_result_send(EB_CMD_GET_AP_INFO, EB_RESULT_FAILED);
}
