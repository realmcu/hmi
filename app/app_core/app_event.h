#ifndef __APP_EVENT_H__
#define __APP_EVENT_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_event.h
 * @brief App-layer event bus (publish / subscribe).
 *
 * Delivery model
 * --------------
 * The event bus does NOT own a task of its own. It shares the single
 * @c app_task created by @c app_core_init() : that task's main loop
 * pulls events off the queue and invokes every matching subscriber
 * serially. All app-layer callbacks therefore run on @c app_task —
 * which means callbacks MUST NOT block (see @c app_module.h ).
 *
 * Publishers copy their payload into a fixed-size slot on the queue and
 * return immediately; dispatch is asynchronous. Because every subscriber
 * runs on the same thread as every other, no cross-subscriber locking is
 * needed — a module wanting genuine parallelism must arrange it inside
 * its own dedicated task.
 *
 * Payload contract
 * ----------------
 * @c payload is copied into the queue (up to @c APP_EVENT_MAX_PAYLOAD
 * bytes). Callers may free / reuse their buffer as soon as @c publish
 * returns. Large data (audio metadata strings, notification bodies, OTA
 * chunks) MUST NOT go through the bus — expose a getter on the owning
 * module instead, or call across modules directly.
 *
 * Lifecycle
 * ---------
 *  - Subscribe only from @c module.init() ; unsubscribe is rarely needed.
 *  - Publish only from @c module.start() onwards. Publishing from
 *    @c init() is illegal because subscribers may not be wired yet.
 *  - Use @ref app_event_publish_isr from interrupt context.
 */

/** Maximum bytes copied into the queue per event. */
#define APP_EVENT_MAX_PAYLOAD   32u

/** Maximum concurrent (event_id, callback) subscriptions across the app. */
#define APP_EVENT_MAX_SUBS      32u

/** Event id — concrete ids live in app_event_defs.h. */
typedef uint16_t app_event_id_t;

/**
 * @brief  Subscriber callback signature.
 *
 * Invoked on the dispatcher task. @c payload points to the copy owned by
 * the queue; it is valid only for the duration of the callback.
 *
 * @param id       event id
 * @param payload  pointer to copied payload (NULL if @c len == 0)
 * @param len      payload byte length
 * @param user     opaque cookie passed at subscribe time
 */
typedef void (*app_event_cb_t)(app_event_id_t id,
                               const void *payload, size_t len,
                               void *user);

/**
 * @brief  One-time event-bus bring-up.
 *
 * Called by @c app_core_init(). Business modules should not call this.
 * Creates the underlying message queue but does NOT start the dispatch
 * loop — that runs on @c app_task via @ref app_event_run_forever .
 *
 * @return 0 on success, negative on failure.
 */
int app_event_bus_init(void);

/**
 * @brief  Event dispatch loop; runs forever on @c app_task .
 *
 * Called from @c app_task 's task body. Blocks pulling events off the
 * queue and invokes every matching subscriber before pulling the next
 * event. Never returns under normal operation.
 */
void app_event_run_forever(void);

/**
 * @brief  Register a callback for one event id.
 *
 * Intended to be called from a module's @c init() . May be called after
 * @c start() as well; there is no race because the subscription table is
 * mutated on the caller side while the dispatcher only reads it.
 *
 * @return 0 on success, negative on failure (e.g. subscription table full).
 */
int app_event_subscribe(app_event_id_t id, app_event_cb_t cb, void *user);

/**
 * @brief  Remove one previously registered (id, cb) pair.
 *
 * @return 0 on success, negative if the entry is not found.
 */
int app_event_unsubscribe(app_event_id_t id, app_event_cb_t cb);

/**
 * @brief  Post an event from a task context.
 *
 * The payload is copied. Delivery is asynchronous.
 *
 * @param id       event id
 * @param payload  data to copy; may be NULL if @c len == 0
 * @param len      byte length, must be <= @c APP_EVENT_MAX_PAYLOAD
 *
 * @return 0 on success, negative on failure (queue full / oversize payload).
 */
int app_event_publish(app_event_id_t id, const void *payload, size_t len);

/**
 * @brief  Post an event from ISR context.
 *
 * Same semantics as @ref app_event_publish but uses the ISR-safe variant
 * of the underlying OSIF message-queue send. Payload is still copied and
 * must fit within @c APP_EVENT_MAX_PAYLOAD.
 *
 * @return 0 on success, negative on failure.
 */
int app_event_publish_isr(app_event_id_t id, const void *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_H__ */
