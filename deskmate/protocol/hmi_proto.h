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

#define PROTO_MAX_PAYLOAD_LEN   236     /* max payload length per frame */

/*============================================================================*
 *                              Types
 *============================================================================*/

typedef int (*proto_send_fn_t)(const uint8_t *data, uint16_t len);
typedef int (*proto_receive_fn_t)(uint8_t *data,       uint16_t max_len);
typedef void (*proto_recv_cb_t)(const uint8_t *data, uint16_t len);

/*============================================================================*
 *                              Functions
 *============================================================================*/

void proto_init(proto_send_fn_t send, proto_receive_fn_t receive, proto_recv_cb_t recv_cb);
bool proto_send(const uint8_t *data, uint16_t len);

/* Called by hmi_protocal_task.c */
int  proto_receive(uint8_t *buf, uint16_t max_len);
void proto_handle(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PROTO_H_ */
