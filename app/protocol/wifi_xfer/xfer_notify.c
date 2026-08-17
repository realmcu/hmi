/**
 * @file    xfer_notify.c
 * @brief   0x13 AP_INFO + 0x16 XFER_FAIL builders (see xfer_notify.h).
 */
#include <string.h>

#include "xfer_notify.h"
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"

void eb_emit_ap_info(const ebadge_softap_info_t *info, uint16_t tcp_port)
{
    if (!info) { return; }

    /* Worst case: (3+32)+(3+63)+4+7+5+4+4 = 125 bytes.  Sized with slack so
     * a max-length SSID + PSK cannot silently drop the trailing TLVs.     */
    uint8_t  params[160];
    uint16_t off   = 0;
    uint16_t pwlen = (uint16_t)strlen(info->password);

    ebadge_tlv_put(params, sizeof(params), &off, EB_TLV_AP_SSID,
                   (const uint8_t *)info->ssid,
                   (uint16_t)strlen(info->ssid));
    ebadge_tlv_put(params, sizeof(params), &off, EB_TLV_AP_PASSWORD,
                   (const uint8_t *)info->password, pwlen);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_CHANNEL,
                      info->channel);
    /* IPv4 goes out in NETWORK order (192.168.4.1 -> C0 A8 04 01), so it
     * cannot use put_u32, which would emit it little-endian.               */
    uint8_t ipb[4] =
    {
        (uint8_t)((info->ip >> 24) & 0xFFu),
        (uint8_t)((info->ip >> 16) & 0xFFu),
        (uint8_t)((info->ip >>  8) & 0xFFu),
        (uint8_t)((info->ip) & 0xFFu),
    };
    ebadge_tlv_put(params, sizeof(params), &off, EB_TLV_AP_IPV4, ipb, 4);
    ebadge_tlv_put_u16(params, sizeof(params), &off, EB_TLV_AP_PORT, tcp_port);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_PROTO,
                      EB_AP_PROTO_RAW_TCP);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_SECURITY,
                      pwlen ? EB_AP_SEC_WPA2_PSK : EB_AP_SEC_OPEN);
    (void)ebadge_l2_notify_send(EB_CMD_AP_INFO, params, off);
}

void eb_emit_fail(uint8_t reason, const char *detail)
{
    uint8_t  params[32];
    uint16_t off = 0;
    ebadge_tlv_put_u8(params, sizeof(params), &off,
                      EB_TLV_FAIL_REASON, reason);
    if (detail && detail[0])
    {
        size_t n = strlen(detail);
        if (n > EB_MAX_MSG_STR) { n = EB_MAX_MSG_STR; }
        ebadge_tlv_put(params, sizeof(params), &off,
                       EB_TLV_FAIL_DETAIL, (const uint8_t *)detail,
                       (uint16_t)n);
    }
    (void)ebadge_l2_notify_send(EB_CMD_XFER_FAIL, params, off);
}
