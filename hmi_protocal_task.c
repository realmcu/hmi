#include <stddef.h>
#include <os_task.h>
#include "hmi_ble_ctrl.h"
#include "hmi_proto.h"
#include "hmi_l2.h"
#include "trace.h"
#include <os_sched.h>

#define PROTO_TASK_STACK_SIZE   4096
#define PROTO_TASK_PRIORITY     3

static void *s_proto_task_handle;

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

void hmi_proto_recv_cb(const uint8_t *data, uint16_t len)
{
    APP_PRINT_INFO2("hmi_proto_recv_cb: len %d, payload %b",
                    len, TRACE_BINARY(len, data));
    hmi_l2_handle(data, len);
}

void hmi_proto_task_init(void)
{
    proto_init(hmi_ble_ctrl_send, hmi_ble_ctrl_receive, hmi_proto_recv_cb);
    os_task_create(&s_proto_task_handle, "proto", proto_task,
                   NULL, PROTO_TASK_STACK_SIZE, PROTO_TASK_PRIORITY);
}
