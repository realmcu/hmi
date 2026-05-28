#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <os_task.h>
#include <os_msg.h>
#include "hmi_ble_ctrl.h"
#include "hmi_proto.h"
#include "hmi_l2.h"
#include "trace.h"
#include <os_sched.h>

#define PROTO_TASK_STACK_SIZE   4096
#define PROTO_TASK_PRIORITY     3

#define L2_TASK_STACK_SIZE      4096
#define L2_TASK_PRIORITY        3
#define L2_QUEUE_SIZE           8

typedef struct
{
    uint8_t  *p_data;   /* malloc'd in recv_cb, freed in l2_task */
    uint16_t  len;
} l2_msg_t;

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

        hmi_l2_handle(msg.p_data, msg.len);
        free(msg.p_data);
    }
}

void hmi_proto_recv_cb(const uint8_t *data, uint16_t len)
{
    APP_PRINT_INFO2("hmi_proto_recv_cb: len %d, payload %b",
                    len, TRACE_BINARY(len, data));

    l2_msg_t msg;
    msg.p_data = malloc(len);
    if (msg.p_data == NULL)
    {
        APP_PRINT_ERROR1("hmi_proto_recv_cb: malloc failed, len %d", len);
        return;
    }
    memcpy(msg.p_data, data, len);
    msg.len = len;

    if (os_msg_send(s_l2_queue_handle, &msg, 0) != true)
    {
        APP_PRINT_ERROR0("hmi_proto_recv_cb: l2 queue full, drop");
        free(msg.p_data);
    }
}

void hmi_proto_task_init(void)
{
    proto_init(hmi_ble_ctrl_send, hmi_ble_ctrl_receive, hmi_proto_recv_cb);

    os_msg_queue_create(&s_l2_queue_handle, "l2Q",
                        L2_QUEUE_SIZE, sizeof(l2_msg_t));

    os_task_create(&s_proto_task_handle, "proto", proto_task,
                   NULL, PROTO_TASK_STACK_SIZE, PROTO_TASK_PRIORITY);
    os_task_create(&s_l2_task_handle, "l2", l2_task,
                   NULL, L2_TASK_STACK_SIZE, L2_TASK_PRIORITY);
}
