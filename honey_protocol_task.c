#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <os_task.h>
#include <os_msg.h>
#include "honey_protocol.h"
#include "honey_protocol_internal.h"
#include "honey_protocol_task.h"
#include "honey_protocol_log.h"
#include <os_sched.h>

#define HONEY_PROTOCOL_TASK_STACK_SIZE   4096
#define HONEY_PROTOCOL_TASK_PRIORITY     3

#define HONEY_PROTOCOL_DISPATCH_TASK_STACK_SIZE  4096
#define HONEY_PROTOCOL_DISPATCH_TASK_PRIORITY    3
#define HONEY_PROTOCOL_QUEUE_SIZE                16

static void *s_honey_protocol_task_handle;
static void *s_dispatch_task_handle;
static void *s_queue_handle;
static honey_protocol_frame_cb_t s_frame_cb;

typedef enum
{
    HONEY_PROTOCOL_MSG_FRAME = 0,
    HONEY_PROTOCOL_MSG_CALL,
} honey_protocol_msg_type_t;

typedef struct
{
    uint16_t type;
    union
    {
        struct
        {
            uint8_t *p_data;
            uint16_t len;
        } frame;
        struct
        {
            honey_protocol_dispatch_fn_t fn;
            void *arg;
        } call;
    } u;
} honey_protocol_msg_t;

typedef struct
{
    uint16_t len;
    bool ack_required;
    uint8_t data[];
} honey_protocol_async_send_t;

static void honey_protocol_task(void *p_param)
{
    uint8_t buf[HONEY_PROTOCOL_MAX_PAYLOAD_LEN + 8]; /* max frame length */

    os_delay(1000); /* wait for system to be ready */

    while (true)
    {
        int n = honey_protocol_receive(buf, sizeof(buf));
        if (n > 0)
        {
            honey_protocol_handle(buf, (uint16_t)n);
        }
    }
}

static void honey_protocol_dispatch_task(void *p_param)
{
    honey_protocol_msg_t msg;

    while (true)
    {
        if (os_msg_recv(s_queue_handle, &msg, 0xFFFFFFFF) != true)
        {
            continue;
        }

        switch (msg.type)
        {
        case HONEY_PROTOCOL_MSG_FRAME:
            if (s_frame_cb != NULL)
            {
                s_frame_cb(msg.u.frame.p_data, msg.u.frame.len);
            }
            free(msg.u.frame.p_data);
            break;

        case HONEY_PROTOCOL_MSG_CALL:
            if (msg.u.call.fn != NULL)
            {
                msg.u.call.fn(msg.u.call.arg);
            }
            break;

        default:
            break;
        }
    }
}

static void honey_protocol_on_frame(const uint8_t *data, uint16_t len)
{
    HONEY_PROTOCOL_LOG_HEX("honey_protocol_on_frame", data, len);

    honey_protocol_msg_t msg;
    msg.type = HONEY_PROTOCOL_MSG_FRAME;
    msg.u.frame.p_data = malloc(len);
    if (msg.u.frame.p_data == NULL)
    {
        HONEY_PROTOCOL_LOG("honey_protocol_on_frame: malloc failed, len %d", len);
        return;
    }
    memcpy(msg.u.frame.p_data, data, len);
    msg.u.frame.len = len;

    if (os_msg_send(s_queue_handle, &msg, 0) != true)
    {
        HONEY_PROTOCOL_LOG("honey_protocol_on_frame: protocol queue full, drop");
        free(msg.u.frame.p_data);
    }
}

bool honey_protocol_run_on_dispatch(honey_protocol_dispatch_fn_t fn, void *arg)
{
    if (fn == NULL || s_queue_handle == NULL)
    {
        return false;
    }

    honey_protocol_msg_t msg;
    msg.type = HONEY_PROTOCOL_MSG_CALL;
    msg.u.call.fn = fn;
    msg.u.call.arg = arg;

    if (os_msg_send(s_queue_handle, &msg, 0) != true)
    {
        HONEY_PROTOCOL_LOG("honey_protocol_run_on_dispatch: protocol queue full, drop");
        return false;
    }
    return true;
}

static void honey_protocol_send_async_cb(void *arg)
{
    honey_protocol_async_send_t *send = arg;
    if (send != NULL)
    {
        (void)honey_protocol_send_direct(send->data, send->len, send->ack_required);
        free(send);
    }
}

static bool honey_protocol_send_queue(const uint8_t *data, uint16_t len, bool ack_required)
{
    if (data == NULL || len == 0u)
    {
        return false;
    }
    honey_protocol_async_send_t *send = malloc(sizeof(*send) + len);
    if (send == NULL)
    {
        return false;
    }
    send->len = len;
    send->ack_required = ack_required;
    memcpy(send->data, data, len);
    if (!honey_protocol_run_on_dispatch(honey_protocol_send_async_cb, send))
    {
        free(send);
        return false;
    }
    return true;
}

static bool honey_protocol_send_with_policy(const uint8_t *data, uint16_t len,
                                   bool ack_required)
{
    void *current = NULL;
    bool on_dispatch = s_dispatch_task_handle != NULL &&
                       os_task_handle_get(&current) &&
                       current == s_dispatch_task_handle;
    if (on_dispatch || s_dispatch_task_handle == NULL)
    {
        return honey_protocol_send_direct(data, len, ack_required);
    }
    return honey_protocol_send_queue(data, len, ack_required);
}

bool honey_protocol_send_with_ack(const uint8_t *data, uint16_t len)
{
    return honey_protocol_send_with_policy(data, len, true);
}

bool honey_protocol_send_without_ack(const uint8_t *data, uint16_t len)
{
    return honey_protocol_send_with_policy(data, len, false);
}

void honey_protocol_task_init(honey_protocol_send_fn_t send, honey_protocol_receive_fn_t receive,
                         honey_protocol_frame_cb_t frame_cb)
{
    s_frame_cb = frame_cb;
    honey_protocol_init(send, receive, honey_protocol_on_frame);

    os_msg_queue_create(&s_queue_handle, "honeyQ",
                        HONEY_PROTOCOL_QUEUE_SIZE, sizeof(honey_protocol_msg_t));

    os_task_create(&s_honey_protocol_task_handle, "honeyProto", honey_protocol_task,
                   NULL, HONEY_PROTOCOL_TASK_STACK_SIZE, HONEY_PROTOCOL_TASK_PRIORITY);
    os_task_create(&s_dispatch_task_handle, "honeyDispatch", honey_protocol_dispatch_task,
                   NULL, HONEY_PROTOCOL_DISPATCH_TASK_STACK_SIZE, HONEY_PROTOCOL_DISPATCH_TASK_PRIORITY);
}
