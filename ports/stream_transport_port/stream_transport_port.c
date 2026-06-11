/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Default porting layer for stream_transport.
 *
 * stream_transport.c calls only the stp_port_* primitives declared in
 * stream_transport_port.h, so the backend can be swapped here without touching
 * the core.  Two ready-made backends are provided and selected automatically -
 * both port straight onto the OS rather than through gui_*, which is what lets
 * a producer create its transport before the GUI task is up, and keeps the
 * shared gui port (a single global queue) out of the picture entirely:
 *
 *   - PC simulator (_HONEYGUI_SIMULATOR_): self-contained POSIX / pthread.
 *     Each queue is an independent mutex + condvar ring buffer; memory is libc
 *     malloc/calloc/free; logging is printf.  No HoneyGUI dependency.
 *
 *   - ARM / RTOS target (default): forward straight to the RTK OS primitives
 *     (malloc / os_msg_* / DBG_DIRECT), mirroring the board port in
 *     board/evb/eBadge/src/ports/realgui_port/gui_port_os.c.
 *
 * For a different RTOS, adjust the ARM branch below, or compile this file with
 * STP_PORT_CUSTOM defined and supply your own stp_port_* definitions elsewhere.
 */

#include "stream_transport_port.h"

#ifndef STP_PORT_CUSTOM

#include <stdarg.h>
#include <stdio.h>      /* vsnprintf */

/* ========================================================================= */
#ifdef _HONEYGUI_SIMULATOR_
/* PC simulator: port straight onto POSIX / pthread so the transport owns its
 * queues outright and never reaches into HoneyGUI's OS abstraction.  Each
 * stp_port_mq_create() builds an independent bounded ring buffer guarded by a
 * mutex + two condvars - one queue per instance, exactly like
 * os_msg_queue_create() on the ARM branch below.  That independence is the
 * whole point: the shared gui port owns a single global queue, so routing the
 * transport's (class_count + 1) queues through it is impossible without
 * butchering the engine.  Porting here on the OS keeps gui_port_os.c pristine
 * and lets a producer stand its transport up before the GUI task exists. */

#include <stdlib.h>     /* malloc / calloc / free      */
#include <string.h>     /* memcpy                      */
#include <pthread.h>
#include <time.h>
#include <sys/time.h>   /* gettimeofday for timed wait */

/* Bounded fixed-slot message queue.  msg_size bytes per slot, max_msgs slots. */
typedef struct stp_mq
{
    uint8_t        *buf;        /* max_msgs * msg_size byte ring   */
    uint32_t        msg_size;   /* bytes per slot                  */
    uint32_t        max_msgs;   /* slot count                      */
    uint32_t        head;       /* next slot to dequeue            */
    uint32_t        tail;       /* next slot to enqueue            */
    uint32_t        count;      /* pending messages                */
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} stp_mq_t;

void *stp_port_malloc(size_t size)
{
    return malloc(size);
}

void *stp_port_calloc(size_t num, size_t size)
{
    return calloc(num, size);
}

void stp_port_free(void *ptr)
{
    free(ptr);
}

/* Bounded wait on @p cond.  The caller handles timeout_ms == 0 (never blocks);
 * 0xFFFFFFFF waits forever, otherwise wait up to that many milliseconds.
 * Returns 0 if (possibly) signalled, non-zero on timeout. */
static int stp_mq_wait(pthread_cond_t *cond, pthread_mutex_t *lock, uint32_t timeout_ms)
{
    if (timeout_ms == 0xFFFFFFFFu)
    {
        return pthread_cond_wait(cond, lock);
    }

    struct timeval now;
#ifdef _WIN32
    mingw_gettimeofday(&now, NULL);
#else
    gettimeofday(&now, NULL);
#endif
    uint64_t deadline_us = (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_usec
                           + (uint64_t)timeout_ms * 1000ULL;
    struct timespec ts;
    ts.tv_sec  = (time_t)(deadline_us / 1000000ULL);
    ts.tv_nsec = (long)((deadline_us % 1000000ULL) * 1000ULL);
    return pthread_cond_timedwait(cond, lock, &ts);
}

bool stp_port_mq_create(void **handle, const char *name, uint32_t msg_size, uint32_t max_msgs)
{
    (void)name;
    if (handle == NULL || msg_size == 0 || max_msgs == 0)
    {
        return false;
    }

    stp_mq_t *mq = (stp_mq_t *)calloc(1, sizeof(*mq));
    if (mq == NULL)
    {
        return false;
    }
    mq->buf = (uint8_t *)malloc((size_t)msg_size * max_msgs);
    if (mq->buf == NULL)
    {
        free(mq);
        return false;
    }
    mq->msg_size = msg_size;
    mq->max_msgs = max_msgs;

    if (pthread_mutex_init(&mq->lock, NULL) != 0)
    {
        free(mq->buf);
        free(mq);
        return false;
    }
    if (pthread_cond_init(&mq->not_empty, NULL) != 0)
    {
        pthread_mutex_destroy(&mq->lock);
        free(mq->buf);
        free(mq);
        return false;
    }
    if (pthread_cond_init(&mq->not_full, NULL) != 0)
    {
        pthread_cond_destroy(&mq->not_empty);
        pthread_mutex_destroy(&mq->lock);
        free(mq->buf);
        free(mq);
        return false;
    }

    *handle = mq;
    return true;
}

bool stp_port_mq_send(void *handle, const void *msg, uint32_t size, uint32_t timeout_ms)
{
    stp_mq_t *mq = (stp_mq_t *)handle;
    (void)size;             /* fixed-size slots, like os_msg_send */
    if (mq == NULL || msg == NULL)
    {
        return false;
    }

    pthread_mutex_lock(&mq->lock);
    while (mq->count == mq->max_msgs)
    {
        if (timeout_ms == 0 || stp_mq_wait(&mq->not_full, &mq->lock, timeout_ms) != 0)
        {
            pthread_mutex_unlock(&mq->lock);
            return false;       /* full (poll) or timed out */
        }
    }

    memcpy(mq->buf + (size_t)mq->tail * mq->msg_size, msg, mq->msg_size);
    mq->tail = (mq->tail + 1) % mq->max_msgs;
    mq->count++;
    pthread_cond_signal(&mq->not_empty);
    pthread_mutex_unlock(&mq->lock);
    return true;
}

bool stp_port_mq_recv(void *handle, void *msg, uint32_t size, uint32_t timeout_ms)
{
    stp_mq_t *mq = (stp_mq_t *)handle;
    (void)size;             /* fixed-size slots, like os_msg_recv */
    if (mq == NULL || msg == NULL)
    {
        return false;
    }

    pthread_mutex_lock(&mq->lock);
    while (mq->count == 0)
    {
        if (timeout_ms == 0 || stp_mq_wait(&mq->not_empty, &mq->lock, timeout_ms) != 0)
        {
            pthread_mutex_unlock(&mq->lock);
            return false;       /* empty (poll) or timed out */
        }
    }

    memcpy(msg, mq->buf + (size_t)mq->head * mq->msg_size, mq->msg_size);
    mq->head = (mq->head + 1) % mq->max_msgs;
    mq->count--;
    pthread_cond_signal(&mq->not_full);
    pthread_mutex_unlock(&mq->lock);
    return true;
}

uint32_t stp_port_mq_count(void *handle)
{
    stp_mq_t *mq = (stp_mq_t *)handle;
    if (mq == NULL)
    {
        return 0;
    }
    pthread_mutex_lock(&mq->lock);
    uint32_t n = mq->count;
    pthread_mutex_unlock(&mq->lock);
    return n;
}

void stp_port_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[STP]%s", buf);
}

/* ========================================================================= */
#else  /* !_HONEYGUI_SIMULATOR_ */
/* ARM / RTOS target.  Mirrors board/evb/eBadge/src/ports/realgui_port/
 * gui_port_os.c so the calls match what the board already compiles against.
 * Adjust the includes / queue API / log sink if your osif differs. */

#include <stdlib.h>     /* malloc / free                            */
#include <string.h>     /* memset (for calloc)                      */
#include <os_msg.h>     /* os_msg_queue_create / send / recv / peek */
#include "trace.h"      /* DBG_DIRECT                               */

void *stp_port_malloc(size_t size)
{
    return malloc(size);
}

void *stp_port_calloc(size_t num, size_t size)
{
    /* gui_port_os.c registers only malloc/free/realloc, so do not assume the
     * RTOS heap redirects calloc - allocate and zero explicitly. */
    size_t total = num * size;
    void *ptr = malloc(total);
    if (ptr != NULL)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}

void stp_port_free(void *ptr)
{
    free(ptr);
}

bool stp_port_mq_create(void **handle, const char *name, uint32_t msg_size, uint32_t max_msgs)
{
    if (handle == NULL)
    {
        return false;
    }
    /* Mirrors gui_port_os.c's port_mq_create.  Note os_msg_queue_create takes
     * (max_msgs, msg_size) - the reverse of this API's (msg_size, max_msgs). */
    return os_msg_queue_create(handle, name, max_msgs, msg_size);
}

bool stp_port_mq_send(void *handle, const void *msg, uint32_t size, uint32_t timeout_ms)
{
    /* Slot size is fixed at create time; os_msg_send copies exactly one slot. */
    (void)size;
    return os_msg_send(handle, (void *)msg, timeout_ms);
}

bool stp_port_mq_recv(void *handle, void *msg, uint32_t size, uint32_t timeout_ms)
{
    (void)size;
    return os_msg_recv(handle, msg, timeout_ms);
}

uint32_t stp_port_mq_count(void *handle)
{
    /* gui_port_os.c carries no count hook; RTK osif reports the pending count
     * through os_msg_queue_peek. */
    uint32_t pending = 0;
    os_msg_queue_peek(handle, &pending);
    return pending;
}

void stp_port_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DBG_DIRECT("[STP]%s", buf);
}

#endif /* _HONEYGUI_SIMULATOR_ */

#else  /* STP_PORT_CUSTOM */

/* The integrator supplies stp_port_* elsewhere; keep this translation unit
 * non-empty so strict toolchains don't warn about it. */
typedef int stp_port_default_disabled_t;

#endif /* STP_PORT_CUSTOM */
