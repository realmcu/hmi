#ifndef _HMI_PROTOCAL_TASK_H_
#define _HMI_PROTOCAL_TASK_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* l2_task queue messages — carry both L2 frames received by BLE and callbacks posted by other tasks
 * that need to execute in l2_task context (to avoid cross-task races on non-thread-safe resources like proto_send).
 * Modeled after wifi_task's T_WIFI_MSG / msg_cb pattern: generic callback, no need for separate types per service. */
typedef enum
{
    L2_MSG_FRAME = 0,   /* L2 frame received by BLE (u.frame) */
    L2_MSG_CALL,        /* Callback posted by other tasks (cb + u.buf/u.param) */
} l2_msg_type_t;

typedef struct l2_msg l2_msg_t;
typedef void (*l2_msg_cb_t)(l2_msg_t *p_msg);

struct l2_msg
{
    uint16_t    type;       /* l2_msg_type_t */
    l2_msg_cb_t cb;         /* When type == L2_MSG_CALL, invoked by l2_task */
    union
    {
        struct
        {
            uint8_t  *p_data;   /* malloc'd by sender, freed by l2_task */
            uint16_t  len;
        } frame;
        uint32_t param;         /* Small payload (by value) */
        void    *buf;           /* Pointer payload, caller convention: cb frees it */
    } u;
};

void hmi_proto_task_init(void);

/* Allows other tasks (e.g., wifi_task) to post a callback for execution in l2_task context.
 * Thread-safe (uses os_msg_send only). If buf is a heap pointer, cb is responsible for freeing it.
 * Returns false if the queue is full or uninitialized (caller must free buf themselves in that case). */
bool hmi_proto_post_call(l2_msg_cb_t cb, void *buf);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PROTOCAL_TASK_H_ */
