#ifndef _HMI_PROTOCAL_TASK_H_
#define _HMI_PROTOCAL_TASK_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* l2_task 队列消息——既承载 BLE 收到的 L2 帧，也承载其它任务投递、需在
 * l2_task 上下文执行的回调（避免跨任务竞争 proto_send 等非线程安全资源）。
 * 仿 wifi_task 的 T_WIFI_MSG / msg_cb 范式：通用回调，不为每种业务单列类型。*/
typedef enum
{
    L2_MSG_FRAME = 0,   /* BLE 收到的 L2 帧（u.frame）*/
    L2_MSG_CALL,        /* 其它任务投递的回调（cb + u.buf/u.param）*/
} l2_msg_type_t;

typedef struct l2_msg l2_msg_t;
typedef void (*l2_msg_cb_t)(l2_msg_t *p_msg);

struct l2_msg
{
    uint16_t    type;       /* l2_msg_type_t */
    l2_msg_cb_t cb;         /* type == L2_MSG_CALL 时由 l2_task 调用 */
    union
    {
        struct
        {
            uint8_t  *p_data;   /* malloc'd by sender, freed by l2_task */
            uint16_t  len;
        } frame;
        uint32_t param;         /* 小负载（按值）*/
        void    *buf;           /* 指针负载，约定由 cb 释放 */
    } u;
};

void hmi_proto_task_init(void);

/* 供其它任务（如 wifi_task）投递一个在 l2_task 上下文执行的回调。
 * 线程安全（仅 os_msg_send）。buf 若为堆指针，约定由 cb 负责释放。
 * 返回 false 表示队列满或未初始化（此时调用方需自行释放 buf）。*/
bool hmi_proto_post_call(l2_msg_cb_t cb, void *buf);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PROTOCAL_TASK_H_ */
