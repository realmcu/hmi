#ifndef _HONEY_PROTOCOL_H_
#define _HONEY_PROTOCOL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*============================================================================*
 *                              Macros
 *============================================================================*/

#define HONEY_PROTOCOL_MAX_PAYLOAD_LEN   (2048 + 8)     /* max payload per frame per spec; BLE L0 layer handles MTU chunking */

/*============================================================================*
 *                              Types
 *============================================================================*/

typedef int (*honey_protocol_send_fn_t)(const uint8_t *data, uint16_t len);
typedef int (*honey_protocol_receive_fn_t)(uint8_t *data,       uint16_t max_len);
typedef void (*honey_protocol_frame_cb_t)(const uint8_t *data, uint16_t len);

/*============================================================================*
 *                              Functions
 *============================================================================*/

/**
 * @brief  Send a framed payload and request an L1 ACK.
 *
 * On the protocol dispatch task this sends synchronously and waits for ACK.
 * From any other task the payload is copied and queued to that task, so the
 * caller never blocks on ACK handling.
 *
 * @param data  Payload pointer; must not exceed HONEY_PROTOCOL_MAX_PAYLOAD_LEN bytes.
 * @param len   Payload length.
 * @return In protocol context: true when ACKed, false on send error/timeout.
 *         Outside protocol context: true when accepted for async delivery.
 */
bool honey_protocol_send_with_ack(const uint8_t *data, uint16_t len);

/**
 * @brief  Send a framed payload without requesting an L1 ACK.
 *
 * Clears the ACK request bit (bit6) in the L1 header. On the protocol dispatch
 * task this returns after the transport send completes. From any other task
 * the payload is copied and queued to the dispatch task, so the caller never
 * blocks on transport I/O.
 *
 * @param data  Payload pointer; must not exceed HONEY_PROTOCOL_MAX_PAYLOAD_LEN bytes.
 * @param len   Payload length.
 * @return In protocol context: true when the transport send succeeds.
 *         Outside protocol context: true when accepted for async delivery.
 */
bool honey_protocol_send_without_ack(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HONEY_PROTOCOL_H_ */
