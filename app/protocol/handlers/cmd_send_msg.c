/**
 * @file    cmd_send_msg.c
 * @brief   0x03 SEND_MSG -- App pushes a notification.  App-layer TODO.
 *
 * Spec §4.3 carries four utf-8 TLVs, each <=23 bytes:
 *   0x01 app name   0x02 title   0x03 text   0x04 date
 * Only `text` is treated as mandatory here; the other three are optional
 * decoration for the UI.
 */
#include <stdint.h>
#include <string.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"

/* Copy a utf-8 TLV into a NUL-terminated scratch buffer, truncating at the
 * §2.8 cap.  Returns the number of bytes copied (0 if the TLV is absent).  */
static uint16_t tlv_to_str(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                           uint8_t type, char *out, uint16_t out_cap)
{
    out[0] = '\0';
    const ebadge_tlv_t *t = ebadge_tlv_find(tlvs, n_tlv, type);
    if (!t || t->len == 0) { return 0; }
    uint16_t n = (t->len < out_cap - 1) ? t->len : (uint16_t)(out_cap - 1);
    memcpy(out, t->val, n);
    out[n] = '\0';
    return n;
}

void handle_send_msg(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    char app_name[EB_MAX_MSG_STR + 1];
    char title[EB_MAX_MSG_STR + 1];
    char text[EB_MAX_MSG_STR + 1];
    char date[EB_MAX_DATE_STR + 1];

    (void)tlv_to_str(tlvs, n_tlv, EB_TLV_MSG_APP_NAME, app_name, sizeof(app_name));
    (void)tlv_to_str(tlvs, n_tlv, EB_TLV_MSG_TITLE,    title,    sizeof(title));
    uint16_t text_len =
        tlv_to_str(tlvs, n_tlv, EB_TLV_MSG_TEXT,       text,     sizeof(text));
    (void)tlv_to_str(tlvs, n_tlv, EB_TLV_MSG_DATE,     date,     sizeof(date));

    if (text_len == 0)
    {
        EBADGE_WARN("SEND_MSG: missing/empty TLV_TEXT -> RESULT FAILED");
        (void)ebadge_l2_result_send(EB_CMD_SEND_MSG, EB_RESULT_FAILED);
        return;
    }

    EBADGE_LOG2("SEND_MSG: app=\"%s\" title=\"%s\"", app_name, title);
    EBADGE_LOG2("SEND_MSG: text=\"%s\" date=\"%s\"", text, date);
    EBADGE_LOG("SEND_MSG: (app-layer UI hook TODO)");

    /* TODO(app): push the four strings to the UI.  They are stack copies here,
     * so if the app consumes them asynchronously it must copy again before
     * post_call'ing -- this frame's storage dies when we return.            */

    (void)ebadge_l2_result_send(EB_CMD_SEND_MSG, EB_RESULT_SUCCEED);
}
