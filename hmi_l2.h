#ifndef _HMI_L2_H_
#define _HMI_L2_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* This component owns only the generic L2 packet envelope and KV dispatch.
 * Command IDs, keys, status codes, and value layouts belong to app_protocol. */

/*============================================================================*
 *                              KV entry (exposed for handlers)
 *============================================================================*/

typedef struct
{
    uint8_t        key;
    uint16_t       val_len;
    const uint8_t *val;
} hmi_l2_kv_t;

/*============================================================================*
 *                              API
 *============================================================================*/

typedef void (*hmi_l2_cmd_handler_t)(const hmi_l2_kv_t *kvs, uint8_t n);

/**
 * @brief  Register a handler for a given L2 command ID.
 *         Call before the first packet arrives (e.g. during system init).
 */
void hmi_l2_register(uint8_t cmd_id, hmi_l2_cmd_handler_t handler);

/**
 * @brief  Parse an L2 packet and dispatch to the registered command handler.
 *
 * @param data  Pointer to the L1 payload (i.e. the full L2 packet: L2 header + L2 payload).
 * @param len   Data length; must be at least 2 bytes (L2 header).
 */
void hmi_l2_handle(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_H_ */
