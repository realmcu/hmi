#ifndef _HMI_PROTOCAL_TASK_H_
#define _HMI_PROTOCAL_TASK_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

 /* l2_task queue message - carries both L2 frames received via BLE and callbacks
  * posted by other tasks that need to execute in l2_task context (avoiding cross-task
  * races on non-thread-safe resources like proto_send).
  * Follows the T_WIFI_MSG / msg_cb pattern from wifi_task: generic callback, no
  * separate types per business domain. */
typedef enum
{
    L2_MSG_FRAME = 0,   /* L2 frame received via BLE (u.frame) */
    L2_MSG_CALL,        /* Callback posted by other tasks (cb + u.buf/u.param) */
} l2_msg_type_t;

typedef struct l2_msg l2_msg_t;
typedef void (*l2_msg_cb_t)(l2_msg_t *p_msg);

struct l2_msg
{
    uint16_t    type;       /* l2_msg_type_t */
    l2_msg_cb_t cb;         /* Called by l2_task when type == L2_MSG_CALL */
    union
    {
        struct
        {
            uint8_t  *p_data;   /* malloc'd by sender, freed by l2_task */
            uint16_t  len;
        } frame;
        uint32_t param;         /* Small payload (by value) */
        void    *buf;           /* Pointer payload, cb is responsible for freeing it */
    } u;
};

void hmi_proto_task_init(void);

 /* Post a callback to be executed in l2_task context from another task (e.g. wifi_task).
  * Thread-safe (uses os_msg_send). If buf is a heap pointer, cb is responsible for freeing it.
  * Returns false if queue is full or not initialized (caller must free buf in that case). */
bool hmi_proto_post_call(l2_msg_cb_t cb, void *buf);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PROTOCAL_TASK_H_ */
