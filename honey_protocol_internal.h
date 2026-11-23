#ifndef _HONEY_PROTOCOL_INTERNAL_H_
#define _HONEY_PROTOCOL_INTERNAL_H_

#include <stdint.h>
#include <stdbool.h>
#include "honey_protocol.h"

void honey_protocol_init(honey_protocol_send_fn_t send, honey_protocol_receive_fn_t receive,
                         honey_protocol_frame_cb_t frame_cb);

int honey_protocol_receive(uint8_t *data, uint16_t max_len);

void honey_protocol_handle(const uint8_t *data, uint16_t len);

/* Synchronous protocol send. Must only be called from the protocol dispatch
 * task, where the transmit frame, sequence counter and ACK wait are serialized. */
bool honey_protocol_send_direct(const uint8_t *data, uint16_t len, bool ack_required);

#endif /* _HONEY_PROTOCOL_INTERNAL_H_ */
