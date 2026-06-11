#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <os_task.h>
#include <os_msg.h>
#include "hmi_ble_ctrl.h"
#include "hmi_proto.h"
#include "hmi_l2.h"
#include "hmi_protocal_task.h"
#include "trace.h"
#include <os_sched.h>

#define PROTO_TASK_STACK_SIZE   4096
#define PROTO_TASK_PRIORITY     3

#define L2_TASK_STACK_SIZE      4096
#define L2_TASK_PRIORITY        3
#define L2_QUEUE_SIZE           8

static void *s_proto_task_handle;
static void *s_l2_task_handle;
static void *s_l2_queue_handle;

static void proto_task(void *p_param)
{
    uint8_t buf[PROTO_MAX_PAYLOAD_LEN + 8]; /* max frame length */

    os_delay(1000); /* wait for system to be ready */

    while (true)
    {
        int n = proto_receive(buf, sizeof(buf));
        if (n > 0)
        {
            proto_handle(buf, (uint16_t)n);
        }
    }
}

static void l2_task(void *p_param)
{
    l2_msg_t msg;

    while (true)
    {
        if (os_msg_recv(s_l2_queue_handle, &msg, 0xFFFFFFFF) != true)
        {
            continue;
        }

        switch (msg.type)
        {
        case L2_MSG_FRAME:
            hmi_l2_handle(msg.u.frame.p_data, msg.u.frame.len);
            free(msg.u.frame.p_data);
            break;

        case L2_MSG_CALL:
            /* 其它任务投递的回调：在 l2_task 上下文执行，避免与 BLE 帧处理
             * 竞争 proto_send 等非线程安全资源 */
            if (msg.cb != NULL)
            {
                msg.cb(&msg);
            }
            break;

        default:
            break;
        }
    }
}

void hmi_on_proto_frame(const uint8_t *data, uint16_t len)
{
    APP_PRINT_INFO2("hmi_on_proto_frame: len %d, payload %b",
                    len, TRACE_BINARY(len, data));

    l2_msg_t msg;
    msg.type = L2_MSG_FRAME;
    msg.cb   = NULL;
    msg.u.frame.p_data = malloc(len);
    if (msg.u.frame.p_data == NULL)
    {
        APP_PRINT_ERROR1("hmi_on_proto_frame: malloc failed, len %d", len);
        return;
    }
    memcpy(msg.u.frame.p_data, data, len);
    msg.u.frame.len = len;

    if (os_msg_send(s_l2_queue_handle, &msg, 0) != true)
    {
        APP_PRINT_ERROR0("hmi_on_proto_frame: l2 queue full, drop");
        free(msg.u.frame.p_data);
    }
}

bool hmi_proto_post_call(l2_msg_cb_t cb, void *buf)
{
    if (cb == NULL || s_l2_queue_handle == NULL)
    {
        return false;
    }

    l2_msg_t msg;
    msg.type  = L2_MSG_CALL;
    msg.cb    = cb;
    msg.u.buf = buf;

    if (os_msg_send(s_l2_queue_handle, &msg, 0) != true)
    {
        APP_PRINT_ERROR0("hmi_proto_post_call: l2 queue full, drop");
        return false;
    }
    return true;
}

void hmi_proto_task_init(void)
{
    proto_init(hmi_ble_ctrl_send, hmi_ble_ctrl_receive, hmi_on_proto_frame);

    os_msg_queue_create(&s_l2_queue_handle, "l2Q",
                        L2_QUEUE_SIZE, sizeof(l2_msg_t));

    os_task_create(&s_proto_task_handle, "proto", proto_task,
                   NULL, PROTO_TASK_STACK_SIZE, PROTO_TASK_PRIORITY);
    os_task_create(&s_l2_task_handle, "l2", l2_task,
                   NULL, L2_TASK_STACK_SIZE, L2_TASK_PRIORITY);
}
