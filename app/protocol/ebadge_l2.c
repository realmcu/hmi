/**
 * @file    ebadge_l2.c
 * @brief   Command dispatch, TLV parse, notify emit.
 */
#include <string.h>
#include "ebadge_l2.h"
#include "ebadge_cmd.h"
#include "ebadge_frame.h"
#include "ebadge_errcode.h"
#include "ebadge_log.h"
#include "port/ebadge_port_ble.h"

/*----------------------------------------------------------------------------*
 *  Handler table
 *----------------------------------------------------------------------------*/
/* We cover cmd IDs 0x01..0x1A -- table size fits with slack. */
#define EBADGE_L2_HANDLER_MAX   0x40

static ebadge_l2_handler_t s_handlers[EBADGE_L2_HANDLER_MAX];

int ebadge_l2_register(uint8_t cmd, ebadge_l2_handler_t handler)
{
    if (cmd == 0 || cmd >= EBADGE_L2_HANDLER_MAX || handler == NULL)
    {
        return EBADGE_ERR_PARAM;
    }
    if (s_handlers[cmd] != NULL)
    {
        EBADGE_WARN1("l2_register: cmd 0x%02x already bound", cmd);
        return EBADGE_ERR_BUSY;
    }
    s_handlers[cmd] = handler;
    return EBADGE_OK;
}

/*----------------------------------------------------------------------------*
 *  TLV parser  (used by ebadge_l2_handle)
 *----------------------------------------------------------------------------*/
static int parse_tlvs(const uint8_t *buf, uint16_t len,
                      ebadge_tlv_t *out, uint8_t out_max, uint8_t *out_n)
{
    uint16_t off = 0;
    uint8_t  n   = 0;

    while (off < len)
    {
        if ((uint16_t)(len - off) < 3)
        {
            return EBADGE_ERR_TLV;               /* stray tail */
        }
        uint8_t  type = buf[off];
        uint16_t tlen = (uint16_t)(buf[off + 1] |
                                   ((uint16_t)buf[off + 2] << 8));
        off = (uint16_t)(off + 3);
        if ((uint32_t)off + tlen > (uint32_t)len)
        {
            return EBADGE_ERR_TLV;
        }
        if (n < out_max)
        {
            out[n].type = type;
            out[n].len  = tlen;
            out[n].val  = (tlen ? (buf + off) : NULL);
            n = (uint8_t)(n + 1);
        }
        else
        {
            EBADGE_WARN1("l2: >%d TLVs, extras dropped", (int)out_max);
            /* silently drop the rest -- caller sees the truncated view      */
        }
        off = (uint16_t)(off + tlen);
    }
    *out_n = n;
    return EBADGE_OK;
}

/*----------------------------------------------------------------------------*
 *  Dispatch
 *----------------------------------------------------------------------------*/
int ebadge_l2_handle(uint8_t cmd, const uint8_t *params, uint16_t params_len)
{
    /* Unified inbound trace: every App->Dev command comes through here,
     * so one line per command gives a stable audit trail during bring-up.  */
    EBADGE_LOG2("<-- CMD 0x%02x plen=%d", cmd, (int)params_len);
    if (params_len)
    {
        EBADGE_LOG_HEX("<-- params", params, params_len);
    }

    if (cmd == 0 || cmd >= EBADGE_L2_HANDLER_MAX)
    {
        EBADGE_WARN1("l2: cmd 0x%02x out of range", cmd);
        return EBADGE_ERR_UNKNOWN_CMD;
    }
    ebadge_l2_handler_t h = s_handlers[cmd];
    if (h == NULL)
    {
        EBADGE_WARN1("l2: no handler for cmd 0x%02x", cmd);
        return EBADGE_ERR_UNKNOWN_CMD;
    }

    ebadge_tlv_t tlvs[EBADGE_L2_MAX_TLV];
    uint8_t      n = 0;
    if (params_len)
    {
        if (parse_tlvs(params, params_len, tlvs, EBADGE_L2_MAX_TLV, &n) < 0)
        {
            EBADGE_WARN1("l2: malformed TLV in cmd 0x%02x", cmd);
            return EBADGE_ERR_TLV;
        }
    }
    EBADGE_LOG2("dispatch cmd=0x%02x n_tlv=%d", cmd, (int)n);
    h(tlvs, n);
    return EBADGE_OK;
}

/*----------------------------------------------------------------------------*
 *  Notify emit
 *----------------------------------------------------------------------------*/
int ebadge_l2_notify_send(uint8_t cmd, const uint8_t *params, uint16_t params_len)
{
    if (!ebadge_port_ble_is_connected())
    {
        EBADGE_WARN1("--> CMD 0x%02x DROP: no link", cmd);
        return EBADGE_ERR_NO_LINK;
    }
    if (!ebadge_port_ble_cccd_enabled())
    {
        EBADGE_WARN1("--> CMD 0x%02x DROP: cccd disabled", cmd);
        return EBADGE_ERR_NO_CCCD;
    }

    uint8_t frame[EBADGE_HDR_LEN + EBADGE_PARAMS_MAX];
    int     total = ebadge_frame_pack(cmd, params, params_len,
                                      frame, sizeof(frame));
    if (total < 0)
    {
        EBADGE_ERR2("--> CMD 0x%02x pack FAIL rc=%d", cmd, total);
        return total;
    }

    /* Unified outbound trace: one line per Dev->App notify. */
    EBADGE_LOG2("--> CMD 0x%02x plen=%d", cmd, (int)params_len);
    if (params_len)
    {
        EBADGE_LOG_HEX("--> params", params, params_len);
    }

    int rc = ebadge_port_ble_notify(frame, (uint16_t)total);
    if (rc != 0)
    {
        EBADGE_ERR2("--> CMD 0x%02x notify FAIL rc=%d", cmd, rc);
    }
    return rc;
}

/*----------------------------------------------------------------------------*
 *  0x04 RESULT helper
 *----------------------------------------------------------------------------*/
int ebadge_l2_result_send(uint8_t cmd_ref, uint8_t code)
{
    uint8_t  params[8];
    uint16_t off = 0;
    /* Spec §4.4 order: TLV_CMD (0x01) then TLV_RESULT (0x02). */
    (void)ebadge_tlv_put_u8(params, sizeof(params), &off,
                            EB_TLV_RESULT_CMD, cmd_ref);
    (void)ebadge_tlv_put_u8(params, sizeof(params), &off,
                            EB_TLV_RESULT_CODE, code);
    EBADGE_LOG2("RESULT: cmd=0x%02x code=%d", cmd_ref, (int)code);
    return ebadge_l2_notify_send(EB_CMD_RESULT, params, off);
}

/*----------------------------------------------------------------------------*
 *  TLV builders
 *----------------------------------------------------------------------------*/
int ebadge_tlv_put(uint8_t *buf, uint16_t cap, uint16_t *poff,
                   uint8_t type, const uint8_t *val, uint16_t len)
{
    if (buf == NULL || poff == NULL || (val == NULL && len != 0))
    {
        return EBADGE_ERR_PARAM;
    }
    if ((uint32_t)*poff + 3 + len > (uint32_t)cap)
    {
        return EBADGE_ERR_NOMEM;
    }
    uint16_t off = *poff;
    buf[off++]   = type;
    buf[off++]   = (uint8_t)(len & 0xFF);
    buf[off++]   = (uint8_t)((len >> 8) & 0xFF);
    if (len)
    {
        memcpy(buf + off, val, len);
    }
    *poff = (uint16_t)(off + len);
    return EBADGE_OK;
}

int ebadge_tlv_put_u8(uint8_t *buf, uint16_t cap, uint16_t *poff,
                      uint8_t type, uint8_t v)
{
    return ebadge_tlv_put(buf, cap, poff, type, &v, 1);
}

int ebadge_tlv_put_u16(uint8_t *buf, uint16_t cap, uint16_t *poff,
                       uint8_t type, uint16_t v)
{
    uint8_t le[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
    return ebadge_tlv_put(buf, cap, poff, type, le, 2);
}

int ebadge_tlv_put_u32(uint8_t *buf, uint16_t cap, uint16_t *poff,
                       uint8_t type, uint32_t v)
{
    uint8_t le[4] =
    {
        (uint8_t)(v & 0xFF),
        (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 24) & 0xFF),
    };
    return ebadge_tlv_put(buf, cap, poff, type, le, 4);
}

int ebadge_tlv_put_u64(uint8_t *buf, uint16_t cap, uint16_t *poff,
                       uint8_t type, uint64_t v)
{
    uint8_t le[8];
    for (int i = 0; i < 8; i++)
    {
        le[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    }
    return ebadge_tlv_put(buf, cap, poff, type, le, 8);
}

/*----------------------------------------------------------------------------*
 *  TLV finders
 *----------------------------------------------------------------------------*/
const ebadge_tlv_t *ebadge_tlv_find(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                                    uint8_t type)
{
    for (uint8_t i = 0; i < n_tlv; i++)
    {
        if (tlvs[i].type == type)
        {
            return &tlvs[i];
        }
    }
    return NULL;
}

bool ebadge_tlv_get_u8(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                       uint8_t type, uint8_t *out)
{
    const ebadge_tlv_t *t = ebadge_tlv_find(tlvs, n_tlv, type);
    if (!t || t->len != 1 || !out) { return false; }
    *out = t->val[0];
    return true;
}

bool ebadge_tlv_get_u16(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                        uint8_t type, uint16_t *out)
{
    const ebadge_tlv_t *t = ebadge_tlv_find(tlvs, n_tlv, type);
    if (!t || t->len != 2 || !out) { return false; }
    *out = (uint16_t)(t->val[0] | ((uint16_t)t->val[1] << 8));
    return true;
}

bool ebadge_tlv_get_u32(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                        uint8_t type, uint32_t *out)
{
    const ebadge_tlv_t *t = ebadge_tlv_find(tlvs, n_tlv, type);
    if (!t || t->len != 4 || !out) { return false; }
    *out = (uint32_t)t->val[0]
           | ((uint32_t)t->val[1] << 8)
           | ((uint32_t)t->val[2] << 16)
           | ((uint32_t)t->val[3] << 24);
    return true;
}
