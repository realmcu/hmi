/*
 * app_event.c
 *
 * Event bus wired onto a single osif message queue. The dispatch loop
 * runs on app_task (see app_core.c / app_event_run_forever).
 *
 * Publish path:
 *   - producer calls app_event_publish(id, payload, len)
 *   - a fixed-size app_event_msg_t is copied into the queue via os_msg_send
 *
 * Dispatch path (app_task body):
 *   - app_event_run_forever() blocks on os_msg_recv
 *   - on receive, iterates the subscription table calling every callback
 *     whose id matches the incoming event
 *
 * Subscription table
 *   - static array of APP_EVENT_MAX_SUBS entries
 *   - subscribe / unsubscribe mutate it from module.init(), before the
 *     dispatch loop starts pulling; no locking needed at bring-up
 *   - post-start, add/remove is rare; we still avoid a lock here because
 *     callbacks themselves run on app_task and any subscribe() called
 *     from a callback is on the same thread — the read/write are naturally
 *     serialised. Subscribe from other threads is currently unsupported.
 */

#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include <os_msg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

APP_LOG_MODULE_REGISTER(app_event);

/* -------- Internal message slot on the queue -------- */

typedef struct
{
    app_event_id_t id;
    uint8_t        len;
    uint8_t        payload[APP_EVENT_MAX_PAYLOAD];
} app_event_msg_t;

/* Queue depth. 32 covers a burst of activity per bring-up sequence. */
#define APP_EVENT_QUEUE_DEPTH   32u

static void *s_queue;   /* osif message queue handle */

/* -------- Subscription table -------- */

typedef struct
{
    app_event_id_t id;
    app_event_cb_t cb;
    void          *user;
} app_event_sub_t;

static app_event_sub_t s_subs[APP_EVENT_MAX_SUBS];
static uint32_t        s_sub_count;

/* -------- API -------- */

int app_event_bus_init(void)
{
    s_sub_count = 0;

    if (!os_msg_queue_create(&s_queue, "app_evt",
                             APP_EVENT_QUEUE_DEPTH,
                             sizeof(app_event_msg_t)))
    {
        APP_LOGE("event queue create failed");
        return -1;
    }
    return 0;
}

int app_event_subscribe(app_event_id_t id, app_event_cb_t cb, void *user)
{
    if (cb == NULL)
    {
        return -1;
    }
    if (s_sub_count >= APP_EVENT_MAX_SUBS)
    {
        APP_LOGE("subscription table full (max=%u)", APP_EVENT_MAX_SUBS);
        return -2;
    }
    s_subs[s_sub_count].id   = id;
    s_subs[s_sub_count].cb   = cb;
    s_subs[s_sub_count].user = user;
    s_sub_count++;
    return 0;
}

int app_event_unsubscribe(app_event_id_t id, app_event_cb_t cb)
{
    for (uint32_t i = 0; i < s_sub_count; ++i)
    {
        if (s_subs[i].id == id && s_subs[i].cb == cb)
        {
            /* Compact by moving the last entry into this slot. */
            s_subs[i] = s_subs[s_sub_count - 1];
            s_sub_count--;
            return 0;
        }
    }
    return -1;
}

/**
 * @brief  Common publish path.
 *
 * @param wait_ms  0 for non-blocking (ISR callers), 0xFFFFFFFF for wait
 *                 forever (task callers with a very cold queue).
 */
static int event_publish_inner(app_event_id_t id, const void *payload,
                               size_t len, uint32_t wait_ms)
{
    if (len > APP_EVENT_MAX_PAYLOAD)
    {
        return -1;
    }
    if (s_queue == NULL)
    {
        return -2;   /* bus_init not run yet */
    }

    app_event_msg_t slot;
    slot.id  = id;
    slot.len = (uint8_t)len;
    if (payload != NULL && len > 0)
    {
        memcpy(slot.payload, payload, len);
    }
    /* Clear the tail so uninitialised bytes don't leak between events. */
    if (len < APP_EVENT_MAX_PAYLOAD)
    {
        memset(slot.payload + len, 0, APP_EVENT_MAX_PAYLOAD - len);
    }

    if (!os_msg_send(s_queue, &slot, wait_ms))
    {
        return -3;   /* queue full */
    }
    return 0;
}

int app_event_publish(app_event_id_t id, const void *payload, size_t len)
{
    /* Task-context publish: block briefly if the queue is momentarily full.
     * We do NOT wait forever here — a stuck consumer would deadlock the
     * producer. 100ms is long enough for a healthy app_task to drain. */
    return event_publish_inner(id, payload, len, 100u);
}

int app_event_publish_isr(app_event_id_t id, const void *payload, size_t len)
{
    /* ISR context: never block. If the queue is full the event is dropped
     * and the caller sees an error — the alternative (block in ISR) would
     * be worse. */
    return event_publish_inner(id, payload, len, 0u);
}

void app_event_run_forever(void)
{
    app_event_msg_t slot;

    for (;;)
    {
        if (!os_msg_recv(s_queue, &slot, 0xFFFFFFFFu))
        {
            /* Should never happen with an infinite wait; guard anyway. */
            continue;
        }

        /* Walk the subscription table. Reading s_sub_count once per
         * iteration is fine — subscribe() writes strictly grow the tail
         * and any concurrent write (only from callbacks on this same
         * thread) will be visible on the next receive. */
        const uint32_t n = s_sub_count;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (s_subs[i].id == slot.id && s_subs[i].cb != NULL)
            {
                s_subs[i].cb(slot.id,
                             (slot.len > 0) ? slot.payload : NULL,
                             slot.len,
                             s_subs[i].user);
            }
        }
    }
}
