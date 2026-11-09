#ifndef _HMI_PROTO_H_
#define _HMI_PROTO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*============================================================================*
 *                              Macros
 *============================================================================*/

#define PROTO_MAX_PAYLOAD_LEN   (2048 + 8)     /* max payload per frame per spec; BLE L0 layer handles MTU chunking */

/*============================================================================*
 *                              Types
 *============================================================================*/

typedef int (*proto_send_fn_t)(const uint8_t *data, uint16_t len);
typedef int (*proto_receive_fn_t)(uint8_t *data,       uint16_t max_len);
typedef void (*proto_recv_cb_t)(const uint8_t *data, uint16_t len);

/*============================================================================*
 *                              Functions
 *============================================================================*/

/**
 * @brief  Initialize the protocol layer with transport and callback functions.
 *
 * @param send      Transport send function: int fn(const uint8_t *data, uint16_t len)
 *                  Returns bytes sent on success, -1 on failure.
 * @param receive   Transport receive function: int fn(uint8_t *data, uint16_t max_len)
 *                  Blocks until data is available; returns bytes received, -1 on failure.
 * @param recv_cb   Upper-layer callback invoked with the decoded payload after CRC passes.
 */
void proto_init(proto_send_fn_t send, proto_receive_fn_t receive, proto_recv_cb_t recv_cb);

/**
 * @brief  Send a framed payload.
 *
 * Wraps the payload in a header (Magic/Version/Len/CRC/Seq), waits for an ACK,
 * and retries up to PROTO_MAX_RETRY (3) times.
 *
 * @param data  Payload pointer; must not exceed PROTO_MAX_PAYLOAD_LEN bytes.
 * @param len   Payload length.
 * @return true  on success (ACK received).
 * @return false on invalid args, send failure, or ACK timeout.
 */
bool proto_send(const uint8_t *data, uint16_t len);

/**
 * @brief  Send a framed payload with an explicit ACK policy.
 *
 * When @p ack_required is true, the ACK request bit is set and the call blocks
 * until a matching ACK arrives or all retries time out. When false, the frame
 * is sent once and the call returns after the transport send completes.
 *
 * @param data          Payload pointer; must not exceed PROTO_MAX_PAYLOAD_LEN bytes.
 * @param len           Payload length.
 * @param ack_required  Whether the receiver must return an L1 ACK.
 * @return true on successful send policy completion, false on failure.
 */
bool proto_send_ex(const uint8_t *data, uint16_t len, bool ack_required);

/**
 * @brief  Send a framed payload without requesting or waiting for an L1 ACK.
 */
bool proto_send_no_ack(const uint8_t *data, uint16_t len);

/**
 * @brief  Blocking raw receive; called in a loop by proto_task.
 *
 * @param buf      Receive buffer; recommended size PROTO_MAX_PAYLOAD_LEN + 8.
 * @param max_len  Buffer capacity.
 * @return Bytes received, or -1 on failure.
 */
int  proto_receive(uint8_t *buf, uint16_t max_len);

/**
 * @brief  Parse raw data and invoke recv_cb when a complete, valid frame is received.
 *         Called in a loop by proto_task.
 *
 * @param data  Raw data pointer.
 * @param len   Data length.
 */
void proto_handle(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PROTO_H_ */
