#ifndef _HONEY_PROTOCOL_TASK_H_
#define _HONEY_PROTOCOL_TASK_H_

#include <stdint.h>
#include <stdbool.h>
#include "honey_protocol.h"          /* honey_protocol_send_fn_t / honey_protocol_receive_fn_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*honey_protocol_dispatch_fn_t)(void *arg);

/**
 * @brief  Bring up the protocol layer.
 *
 * The caller (app layer) injects the transport primitives, so this
 * component stays independent of any specific transport (BLE, SPP, UART).
 * Creates the protocol queue, then spawns @c honey_protocol_task (drains and parses the
 * transport receive path) and the dispatch task (forwards decoded L1 payloads
 * to @p frame_cb and runs cross-task callbacks).
 *
 * @param send     Transport send: int fn(const uint8_t *data, uint16_t len).
 *                 Returns bytes sent on success, -1 on failure.
 * @param receive  Blocking transport receive:
 *                 int fn(uint8_t *buf, uint16_t max_len).
 *                 Returns bytes received, -1 on failure.
 * @param frame_cb Upper-layer callback for each decoded L1 payload. The
 *                 application owns L2 parsing and supplies this callback.
 */
void honey_protocol_task_init(honey_protocol_send_fn_t send, honey_protocol_receive_fn_t receive,
                              honey_protocol_frame_cb_t frame_cb);

/**
 * @brief  Run a callback in the protocol dispatch task context.
 *
 * The callback is queued and executes asynchronously, serialized with inbound
 * L2 frame handling and synchronous protocol sends. The callback must be short
 * and must not block the dispatch task. If arg owns heap memory, the callback
 * is responsible for releasing it.
 *
 * @return true when the callback was queued; false when the queue is full or
 *         the protocol task has not been initialized.
 */
bool honey_protocol_run_on_dispatch(honey_protocol_dispatch_fn_t fn, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _HONEY_PROTOCOL_TASK_H_ */
