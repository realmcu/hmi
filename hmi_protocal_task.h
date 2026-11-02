#ifndef _HMI_PROTOCAL_TASK_H_
#define _HMI_PROTOCAL_TASK_H_

#include <stdint.h>
#include <stdbool.h>
#include "hmi_proto.h"          /* proto_send_fn_t / proto_receive_fn_t */

#ifdef __cplusplus
extern "C" {
#endif

 /* Protocol queue message - carries both L1 payloads received via BLE and callbacks
  * posted by other tasks that need to execute in protocol dispatch context (avoiding cross-task
  * races on non-thread-safe resources like proto_send).
  * Follows the T_WIFI_MSG / msg_cb pattern from wifi_task: generic callback, no
  * separate types per business domain. */
typedef enum
{
    PROTO_MSG_FRAME = 0,   /* Decoded L1 payload received via BLE (u.frame) */
    PROTO_MSG_CALL,        /* Callback posted by other tasks (cb + u.buf/u.param) */
} proto_msg_type_t;

typedef struct proto_msg proto_msg_t;
typedef void (*proto_msg_cb_t)(proto_msg_t *p_msg);

struct proto_msg
{
    uint16_t        type;       /* proto_msg_type_t */
    proto_msg_cb_t  cb;         /* Called by the dispatch task when type == PROTO_MSG_CALL */
    union
    {
        struct
        {
            uint8_t  *p_data;   /* malloc'd by sender, freed by dispatch task */
            uint16_t  len;
        } frame;
        uint32_t param;         /* Small payload (by value) */
        void    *buf;           /* Pointer payload, cb is responsible for freeing it */
    } u;
};

/**
 * @brief  Bring up the protocol layer.
 *
 * The caller (app layer) injects the transport primitives, so this
 * component stays independent of any specific transport (BLE, SPP, UART).
 * Creates the protocol queue, then spawns @c proto_task (drains the transport
 * receive path into @c proto_handle ) and the dispatch task (forwards decoded
 * L1 payloads to @p frame_cb and runs cross-task callbacks).
 *
 * @param send     Transport send: int fn(const uint8_t *data, uint16_t len).
 *                 Returns bytes sent on success, -1 on failure.
 * @param receive  Blocking transport receive:
 *                 int fn(uint8_t *buf, uint16_t max_len).
 *                 Returns bytes received, -1 on failure.
 * @param frame_cb Upper-layer callback for each decoded L1 payload. The
 *                 application owns L2 parsing and supplies this callback.
 */
void hmi_proto_task_init(proto_send_fn_t send, proto_receive_fn_t receive,
                         proto_recv_cb_t frame_cb);

 /* Post a callback to be executed in protocol dispatch context from another task (e.g. wifi_task).
  * Thread-safe (uses os_msg_send). If buf is a heap pointer, cb is responsible for freeing it.
  * Returns false if queue is full or not initialized (caller must free buf in that case). */
bool hmi_proto_post_call(proto_msg_cb_t cb, void *buf);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PROTOCAL_TASK_H_ */
