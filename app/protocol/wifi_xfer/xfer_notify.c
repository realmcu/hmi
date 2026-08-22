/**
 * @file    xfer_notify.c
 * @brief   0x13 AP_INFO + 0x16 XFER_FAIL builders (see xfer_notify.h).
 */
#include <string.h>

#include "xfer_notify.h"
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"

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
    /* The port is the CALLER's to choose and this function must not second-guess
     * it: the file session wants FILE_PORT= (9000) and the preview session wants
     * PORT= (5004), and only they know which.  There was a `tcp_port = 9000;`
     * here during bring-up which overrode both, so every 0x13 -- preview
     * included -- advertised the upload port and the preview stream had nowhere
     * to land. */
    ebadge_tlv_put_u16(params, sizeof(params), &off, EB_TLV_AP_PORT, tcp_port);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_PROTO,
                      EB_AP_PROTO_RAW_TCP);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_SECURITY,
                      pwlen ? EB_AP_SEC_WPA2_PSK : EB_AP_SEC_OPEN);
    /* Log the credentials as the phone will receive them, at the last moment
     * before they go on the wire.  This is the only line that can be trusted to
     * answer "what was the phone told?" -- the AT-layer parse logs describe a
     * reply, and a reply is not what we forwarded: the cache merges, so a parsed
     * ip=0 is discarded rather than sent.  Reading a zero there as "we sent the
     * phone a zero" is a wrong turn worth designing out.
     *
     * Password logged as a length only.  The SSID is broadcast anyway, but a PSK
     * in a bring-up log tends to outlive the bring-up. */
    EBADGE_LOG(EB_DIR_TO_PHONE "0x13 AP_INFO: ssid=\"%s\" pw_len=%u ip=%u.%u.%u.%u"
               " port=%u channel=%u",
               info->ssid, (unsigned)pwlen,
               ipb[0], ipb[1], ipb[2], ipb[3],
               (unsigned)tcp_port, (unsigned)info->channel);
    if (tcp_port == 0U || info->ip == 0U || info->ssid[0] == '\0')
    {
        /* Should be unreachable: port_softap refuses to start a session without
         * all three.  If it fires, the offending field reached here from
         * somewhere that bypassed that check, and the phone is about to be sent
         * somewhere that does not exist. */
        EBADGE_WARN(EB_DIR_TO_PHONE "0x13 AP_INFO is incomplete -- the phone "
                    "cannot reach this");
    }
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
