/**
 * @file    ebadge_l2.h
 * @brief   Above-frame layer:  TLV parsing, command dispatch, notify emit.
 *
 * The frame layer hands us (cmd, params, params_len); here we walk the TLVs,
 * look up the registered handler for that cmd and call it.  Notify emissions
 * (Dev -> App on the EVENT char) also live here so all outbound goes through
 * one place -- easy CCCD-gating.
 */
#ifndef _EBADGE_L2_H_
#define _EBADGE_L2_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  TLV -- parsed view
 *----------------------------------------------------------------------------*/
#define EBADGE_L2_MAX_TLV       16     /* per-frame TLV cap                  */

typedef struct
{
    uint8_t         type;
    uint16_t        len;
    const uint8_t  *val;               /* pointer into caller-owned buffer   */
} ebadge_tlv_t;

/*----------------------------------------------------------------------------*
 *  Handler registry
 *----------------------------------------------------------------------------*/
typedef void (*ebadge_l2_handler_t)(const ebadge_tlv_t *tlvs, uint8_t n_tlv);

/**
 * @brief  Register a handler for cmd id.
 * @return 0 on success, negative on error (bad cmd / already registered).
 */
int  ebadge_l2_register(uint8_t cmd, ebadge_l2_handler_t handler);

/**
 * @brief  Dispatch a decoded frame to the registered handler after parsing
 *         its TLV list.  Called from ebadge_task by the frame layer cb.
 *
 * If no handler is registered we return EBADGE_ERR_UNKNOWN_CMD; the caller
 * decides whether to send a 0x04 RESULT back or drop.
 *
 * @return 0 on success, negative otherwise.
 */
int  ebadge_l2_handle(uint8_t cmd, const uint8_t *params, uint16_t params_len);

/*----------------------------------------------------------------------------*
 *  Outbound (Notify on EVENT char)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Pack + send one command notify.  Must be called on l2_task context.
 *
 * Behaviour if the peer has not enabled the EVENT CCCD:  DROP the notify
 * and emit a warning log (per user decision -- see design notes).
 *
 * @param  cmd         Command id (EB_CMD_*).
 * @param  params      TLV-encoded params bytes (may be NULL if plen == 0).
 * @param  params_len  Length of @p params.
 * @return 0 on send-issued success, negative on error.
 */
int  ebadge_l2_notify_send(uint8_t cmd, const uint8_t *params, uint16_t params_len);

/**
 * @brief  Send a 0x04 RESULT answering @p cmd_ref.
 *
 * Every H->D command that has no dedicated reply notify acks through this.
 * Centralised so the two TLVs always go out in spec §4.4 order
 * (TLV_CMD 0x01 first, TLV_RESULT 0x02 second).
 *
 * @param  cmd_ref  The command id being answered (EB_CMD_*).
 * @param  code     EB_RESULT_SUCCEED or EB_RESULT_FAILED -- note 0 is FAILURE.
 */
int  ebadge_l2_result_send(uint8_t cmd_ref, uint8_t code);

/*----------------------------------------------------------------------------*
 *  TLV builders  (write into caller-provided cursor)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Append one TLV to @p buf at cursor @p *poff.
 * @return 0 on success, negative on overflow.
 */
int  ebadge_tlv_put(uint8_t *buf, uint16_t cap, uint16_t *poff,
                    uint8_t type, const uint8_t *val, uint16_t len);
int  ebadge_tlv_put_u8(uint8_t *buf, uint16_t cap, uint16_t *poff,
                       uint8_t type, uint8_t  v);
int  ebadge_tlv_put_u16(uint8_t *buf, uint16_t cap, uint16_t *poff,
                        uint8_t type, uint16_t v);
int  ebadge_tlv_put_u32(uint8_t *buf, uint16_t cap, uint16_t *poff,
                        uint8_t type, uint32_t v);
/** 8-byte LE integer -- needed by 0x1A STORAGE_INFO (spec §4.12). */
int  ebadge_tlv_put_u64(uint8_t *buf, uint16_t cap, uint16_t *poff,
                        uint8_t type, uint64_t v);

/*----------------------------------------------------------------------------*
 *  TLV finder helpers  (linear scan; TLV counts are small)
 *----------------------------------------------------------------------------*/
const ebadge_tlv_t *ebadge_tlv_find(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                                    uint8_t type);
bool  ebadge_tlv_get_u8(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                        uint8_t type, uint8_t  *out);
bool  ebadge_tlv_get_u16(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                         uint8_t type, uint16_t *out);
bool  ebadge_tlv_get_u32(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                         uint8_t type, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_L2_H_ */
