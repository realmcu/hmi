/**
 * @file    ebadge_task.h
 * @brief   Serialisation task (l2_task) for the eBadge V1.2 stack.
 *
 * All state-mutating protocol work happens on this one task, driven by an
 * os_msg queue.  Message kinds:
 *
 *   RX_BYTES  -- opaque BLE payload from port_ble (caller frees on free_cb)
 *   POST_CALL -- generic function pointer + arg, used to marshal from other
 *                threads (softap / tcp / user UI) into the l2_task context
 *   TIMER_TICK-- fired by a low-rate periodic tick (~100ms) so xfer_session
 *                can time out without a dedicated OS timer  (see design)
 *
 * Everything else (frame reassembly, TLV parse, xfer_session state, notify
 * emission) is single-threaded on this task -- no locks anywhere.
 */
#ifndef _EBADGE_TASK_H_
#define _EBADGE_TASK_H_

#include <stdint.h>
#include <stdbool.h>
#include "ebadge_frame.h"        /* ebadge_raw_cb_t */

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Startup
 *----------------------------------------------------------------------------*/
/**
 * @brief  Create the l2_task, msg queue, register handler table.
 *         Called from main.c after BLE stack is up.  Idempotent.
 */
int ebadge_task_init(void);

/*----------------------------------------------------------------------------*
 *  RX ingress  (from port_ble on ATT write)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Enqueue a BLE ATT write payload for framing.  Safe from BT stack
 *         context.  We take a copy (malloc) -- port_ble may reuse its buffer.
 *
 * @return 0 on enqueue, negative on OOM / queue full (payload dropped).
 */
int ebadge_task_on_rx(const uint8_t *data, uint16_t len);

/*----------------------------------------------------------------------------*
 *  Cross-thread post_call
 *----------------------------------------------------------------------------*/
typedef void (*ebadge_post_fn_t)(void *arg);

/**
 * @brief  Ask the l2_task to invoke fn(arg) on its own context.  Safe from
 *         any thread.  If @p arg was heap-allocated the callback is
 *         responsible for freeing it.
 */
int ebadge_task_post_call(ebadge_post_fn_t fn, void *arg);

/*----------------------------------------------------------------------------*
 *  Tick timer  (mocked with OS timer feeding a POST_CALL each 100ms)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Register a periodic tick handler (called ~ every EBADGE_TICK_MS ms
 *         on the l2_task).  Both Wi-Fi sessions use this for their timeouts.
 *
 * Up to EBADGE_TICK_SINKS sinks are fanned out, in registration order.
 * Registering the same fn twice is a no-op.  There is no unregister: sinks
 * live for the life of the process and must tolerate ticking while idle.
 */
typedef void (*ebadge_tick_fn_t)(uint32_t now_ms);
void ebadge_task_set_tick(ebadge_tick_fn_t fn);

/** Number of tick sinks the fanout can hold (xfer_session, stream_session). */
#define EBADGE_TICK_SINKS   4

/** Nominal tick period.  See xfer_session timeout constants. */
#define EBADGE_TICK_MS   100

/*----------------------------------------------------------------------------*
 *  Raw body pass-through  (0x02 SEND_FILE, spec §4.2)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Divert the next @p len RX stream bytes to @p cb instead of framing
 *         them.  Thin wrapper over ebadge_frame_expect_raw() on the task's
 *         own reassembler, which handlers cannot otherwise reach.
 *
 * MUST be called from inside a command handler (i.e. on l2_task, within the
 * frame callback).  Anywhere else it is refused and returns non-zero.
 *
 * @return 0 on success, negative otherwise (see ebadge_status_t).
 */
int ebadge_task_expect_raw(uint32_t len, ebadge_raw_cb_t cb, void *user);

/**
 * @brief  Drop any partially-received frame / raw body and resume clean
 *         framing.  Call on BLE disconnect, on l2_task context.
 */
void ebadge_task_rx_reset(void);

/*----------------------------------------------------------------------------*
 *  Wall-clock helper  (monotonic-ish, ms since boot; wraps uint32_t)
 *----------------------------------------------------------------------------*/
uint32_t ebadge_task_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_TASK_H_ */
