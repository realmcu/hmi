/**
 * @file    ebadge_task.c
 * @brief   Single-threaded serialiser for the eBadge V1.2 stack.
 *
 * All state mutation (frame reassembly, TLV parse, dispatch, xfer_session,
 * notify emit) runs here.  Everything else (BLE stack cb, SoftAP cb, TCP
 * cb, user UI) marshals in via ebadge_task_on_rx() / _post_call() below.
 *
 * The tick timer feeds an internal POST_CALL every EBADGE_TICK_MS so
 * xfer_session can enforce §5.5 deadlines without a per-session OS timer.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <os_task.h>
#include <os_msg.h>
#include <os_timer.h>
#include <os_sched.h>

#include "ebadge_task.h"
#include "ebadge_frame.h"
#include "ebadge_l2.h"
#include "ebadge_errcode.h"
#include "ebadge_log.h"
#include "handlers/handlers_register.h"
#include "wifi_xfer/xfer_session.h"
#include "wifi_xfer/stream_session.h"
#include "wifi_xfer/jpgs_ingress.h"
#include "wifi_xfer/ebfs_ingress.h"
#include "port/ebadge_port_ble.h"
#include "port/ebadge_port_softap.h"
#include "port/ebadge_port_vbat.h"

/*----------------------------------------------------------------------------*
 *  Configuration
 *----------------------------------------------------------------------------*/
#define L2_TASK_STACK_SIZE   4096
#define L2_TASK_PRIORITY     3
#define L2_QUEUE_DEPTH       16          /* deeper than old proto (batched RX)*/

/*----------------------------------------------------------------------------*
 *  Message types
 *----------------------------------------------------------------------------*/
enum
{
    L2_MSG_RX_BYTES  = 1,
    L2_MSG_POST_CALL = 2,
    L2_MSG_TICK      = 3,
};

typedef struct
{
    uint8_t   type;
    union
    {
        struct
        {
            uint8_t  *data;              /* malloc'd, freed after handle    */
            uint16_t  len;
        } rx;
        struct
        {
            ebadge_post_fn_t fn;
            void            *arg;
        } post;
        struct
        {
            uint32_t now_ms;
        } tick;
    } u;
} ebadge_msg_t;

/*----------------------------------------------------------------------------*
 *  Module state  (l2_task-owned)
 *----------------------------------------------------------------------------*/
static void            *s_task_handle;
static void            *s_queue_handle;
static void            *s_tick_timer;
static ebadge_rx_ctx_t  s_rx_ctx;
static ebadge_tick_fn_t s_tick_fn[EBADGE_TICK_SINKS];
static uint8_t          s_tick_n;
static bool             s_inited;

/*----------------------------------------------------------------------------*
 *  Frame callback  (called from within ebadge_frame_feed on l2_task)
 *----------------------------------------------------------------------------*/
static void on_frame(uint8_t ver, uint8_t cmd, uint8_t flags,
                     const uint8_t *params, uint16_t params_len,
                     void *user)
{
    (void)ver; (void)flags; (void)user;
    EBADGE_LOG2("frame in: cmd=0x%02x plen=%d", cmd, params_len);
    (void)ebadge_l2_handle(cmd, params, params_len);
}

/*----------------------------------------------------------------------------*
 *  Tick timer callback  (os_timer / bt-timer context -- must post)
 *----------------------------------------------------------------------------*/
static void tick_timer_cb(void *p_handle)
{
    (void)p_handle;
    ebadge_msg_t msg;
    msg.type          = L2_MSG_TICK;
    msg.u.tick.now_ms = ebadge_task_now_ms();
    /* If queue is full we simply drop a tick -- timeouts have >= 30s slack. */
    (void)os_msg_send(s_queue_handle, &msg, 0);
}

/*----------------------------------------------------------------------------*
 *  l2_task main loop
 *----------------------------------------------------------------------------*/
static void l2_task_entry(void *p_param)
{
    (void)p_param;
    ebadge_msg_t msg;

    while (true)
    {
        if (os_msg_recv(s_queue_handle, &msg, 0xFFFFFFFF) != true)
        {
            continue;
        }
        switch (msg.type)
        {
        case L2_MSG_RX_BYTES:
            (void)ebadge_frame_feed(&s_rx_ctx, msg.u.rx.data, msg.u.rx.len,
                                    on_frame, NULL);
            free(msg.u.rx.data);
            break;

        case L2_MSG_POST_CALL:
            if (msg.u.post.fn)
            {
                msg.u.post.fn(msg.u.post.arg);
            }
            break;

        case L2_MSG_TICK:
            for (uint8_t i = 0; i < s_tick_n; i++)
            {
                s_tick_fn[i](msg.u.tick.now_ms);
            }
            break;

        default:
            break;
        }
    }
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
int ebadge_task_init(void)
{
    if (s_inited)
    {
        return EBADGE_OK;
    }

    ebadge_frame_rx_reset(&s_rx_ctx);

    if (os_msg_queue_create(&s_queue_handle, "eb-l2",
                            L2_QUEUE_DEPTH, sizeof(ebadge_msg_t)) != true)
    {
        EBADGE_ERR("task_init: msgQ create failed");
        return EBADGE_ERR_INTERNAL;
    }

    if (os_task_create(&s_task_handle, "eb-l2", l2_task_entry,
                       NULL, L2_TASK_STACK_SIZE, L2_TASK_PRIORITY) != true)
    {
        EBADGE_ERR("task_init: task create failed");
        return EBADGE_ERR_INTERNAL;
    }

    /* Periodic tick -- fires POST_CALL into l2_task every EBADGE_TICK_MS. */
    if (os_timer_create(&s_tick_timer, "eb-tick", 0 /* id */,
                        EBADGE_TICK_MS, true /* reload */, tick_timer_cb) != true)
    {
        EBADGE_ERR("task_init: tick timer create failed");
        /* not fatal for RX; xfer_session timeouts will not fire */
    }
    else
    {
        os_timer_start(&s_tick_timer);
    }

    s_inited = true;

    /* Once the task is up, wire the pieces around it: BLE port, the SoftAP
     * port (registers a tick sink and starts priming the AP cache), both Wi-Fi
     * state machines (each registers a tick sink too), then command handlers
     * (registers the L2 dispatch table).  Order matters only in that handlers
     * can call into either session, so sessions go first.
     *
     * port_softap goes before the sessions so its cache has had the longest
     * possible head start by the time the first offer arrives -- an offer with
     * nothing cached is answered AP_START, and the AP query it depends on takes
     * seconds over the 8711's SPI link.                                    */
    ebadge_port_ble_init();
    ebadge_port_softap_init();
    /* Registers a tick sink and submits the first ADC conversion, so the very
     * first GET_BATTERY has a chance of finding a real voltage rather than the
     * placeholder.  Independent of everything else here -- a failure only means
     * the battery reads as unknown. */
    ebadge_port_vbat_init();
    xfer_session_init();
    stream_session_init();
    jpgs_ingress_init();
    ebfs_ingress_init();
    ebadge_handlers_register();

    EBADGE_LOG("l2_task up");
    return EBADGE_OK;
}

int ebadge_task_on_rx(const uint8_t *data, uint16_t len)
{
    if (!s_inited || data == NULL || len == 0)
    {
        return EBADGE_ERR_PARAM;
    }
    ebadge_msg_t msg;
    msg.type       = L2_MSG_RX_BYTES;
    msg.u.rx.data  = (uint8_t *)malloc(len);
    if (msg.u.rx.data == NULL)
    {
        EBADGE_ERR1("on_rx: OOM len=%d", len);
        return EBADGE_ERR_NOMEM;
    }
    memcpy(msg.u.rx.data, data, len);
    msg.u.rx.len   = len;

    if (os_msg_send(s_queue_handle, &msg, 0) != true)
    {
        EBADGE_ERR("on_rx: queue full, drop");
        free(msg.u.rx.data);
        return EBADGE_ERR_BUSY;
    }
    return EBADGE_OK;
}

int ebadge_task_post_call(ebadge_post_fn_t fn, void *arg)
{
    if (!s_inited || fn == NULL)
    {
        return EBADGE_ERR_PARAM;
    }
    ebadge_msg_t msg;
    msg.type        = L2_MSG_POST_CALL;
    msg.u.post.fn   = fn;
    msg.u.post.arg  = arg;
    if (os_msg_send(s_queue_handle, &msg, 0) != true)
    {
        EBADGE_ERR("post_call: queue full, drop");
        return EBADGE_ERR_BUSY;
    }
    return EBADGE_OK;
}

int ebadge_task_expect_raw(uint32_t len, ebadge_raw_cb_t cb, void *user)
{
    if (!s_inited)
    {
        return EBADGE_ERR_PARAM;
    }
    return ebadge_frame_expect_raw(&s_rx_ctx, len, cb, user);
}

void ebadge_task_rx_reset(void)
{
    /* Must run on l2_task -- callers from other contexts post_call this.
     * Dropping a partially-received frame or raw body on disconnect keeps a
     * stale byte count from eating the next connection's first frames.     */
    ebadge_frame_rx_reset(&s_rx_ctx);
}

void ebadge_task_set_tick(ebadge_tick_fn_t fn)
{
    if (fn == NULL)
    {
        return;
    }
    for (uint8_t i = 0; i < s_tick_n; i++)
    {
        if (s_tick_fn[i] == fn) { return; }     /* already registered        */
    }
    if (s_tick_n >= EBADGE_TICK_SINKS)
    {
        /* Silently dropping a sink means a session never times out, which is
         * far worse to debug than a loud complaint at init time.            */
        EBADGE_ERR1("set_tick: sink table full (%d), timeout sink LOST",
                    (int)s_tick_n);
        return;
    }
    s_tick_fn[s_tick_n++] = fn;
}

uint32_t ebadge_task_now_ms(void)
{
    /* os_sys_time_get() returns a 64-bit Zephyr k_uptime_get()-derived ms tick;
     * truncate to 32 bits.  The protocol only uses now_ms in unsigned-diff
     * comparisons (deadline - now, last_data_ms - now), which stay correct
     * across the 49.7-day wrap -- and every session has a 180s overall cap
     * anyway, so wrap during one xfer is impossible. */
    return (uint32_t)os_sys_time_get();
}
