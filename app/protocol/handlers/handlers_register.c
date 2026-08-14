/**
 * @file    handlers_register.c
 * @brief   Central registration point.  Adding a new command means:
 *
 *          1) declare the handler function (see below "extern void ..._handle")
 *          2) add an ebadge_l2_register(...) line here
 *          3) implement the handler in its own cmd_*.c file
 *
 *          Keeping the table here means one place to audit for coverage
 *          against PROT-001 §4 command catalogue.
 */
#include "handlers_register.h"
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_log.h"

/* Handler forward declarations -- one per command. */
extern void handle_set_time(const ebadge_tlv_t *tlvs, uint8_t n);
extern void handle_send_file(const ebadge_tlv_t *tlvs, uint8_t n);
extern void handle_send_msg(const ebadge_tlv_t *tlvs, uint8_t n);
extern void handle_result(const ebadge_tlv_t *tlvs, uint8_t n);
extern void handle_xfer_offer(const ebadge_tlv_t *tlvs, uint8_t n);
extern void handle_get_ap_info(const ebadge_tlv_t *tlvs, uint8_t n);
extern void handle_get_battery(const ebadge_tlv_t *tlvs, uint8_t n);
extern void handle_get_storage(const ebadge_tlv_t *tlvs, uint8_t n);

void ebadge_handlers_register(void)
{
    /* App -> Dev inbound.  Notifies (0x11/0x13/0x14/0x15/0x16/0x18/0x1A)
     * are outbound-only and have no handler here.                       */
    (void)ebadge_l2_register(EB_CMD_SET_TIME,     handle_set_time);
    (void)ebadge_l2_register(EB_CMD_SEND_FILE,    handle_send_file);
    (void)ebadge_l2_register(EB_CMD_SEND_MSG,     handle_send_msg);
    (void)ebadge_l2_register(EB_CMD_RESULT,       handle_result);
    (void)ebadge_l2_register(EB_CMD_XFER_OFFER,   handle_xfer_offer);
    (void)ebadge_l2_register(EB_CMD_GET_AP_INFO,  handle_get_ap_info);
    (void)ebadge_l2_register(EB_CMD_GET_BATTERY,  handle_get_battery);
    (void)ebadge_l2_register(EB_CMD_GET_STORAGE,  handle_get_storage);

    EBADGE_LOG("handlers registered");
}
