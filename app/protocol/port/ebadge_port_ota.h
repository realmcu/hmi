/**
 * @file    ebadge_port_ota.h
 * @brief   Porting layer for reflashing the Wi-Fi module.  What the protocol
 *          stack is allowed to know about the 8711's OTA path.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS HIDES, AND WHY IT IS NOT JUST INDIRECTION
 * ---------------------------------------------------------------------------
 * Two things, both of which would otherwise leak into ota_session.c:
 *
 *   - the transport.  Below here it is AT+OTAUSER in transparent mode over SPI;
 *     above here it is "push these bytes at the module".  The session has no
 *     business knowing either, exactly as it has no business knowing that the
 *     SoftAP is a command string.
 *   - the thread.  at_ota's callbacks arrive on at_client's threads -- the SPI
 *     transport thread (priority 5) or the system workqueue -- and l2_task
 *     (priority 3) preempts both.  Session state has no locks, so a completion
 *     delivered directly could land in the middle of a teardown.  Everything
 *     below marshals through ebadge_task_post_call() first.
 *
 * ---------------------------------------------------------------------------
 * PROGRESS IS PULLED, NOT PUSHED
 * ---------------------------------------------------------------------------
 * A megabyte is ~64 chunks, so a pushed progress callback would be ~64 hops
 * through the l2 queue for information the session only reports at a throttle
 * anyway.  So the byte counter is a plain 32-bit read (ebadge_port_ota_sent())
 * and the session samples it from the tick it already runs for its timeouts.
 *
 * That also gives the session a stall watchdog for free: a counter that stops
 * advancing is a push that has stopped, which is a distinct fault from a push
 * that reported an error.
 *
 * CONTEXT: every function here is safe to call from l2_task, and the completion
 * callback is delivered there.  ebadge_port_ota_sent() is safe from anywhere.
 */
#ifndef _EBADGE_PORT_OTA_H_
#define _EBADGE_PORT_OTA_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  The push finished.  Delivered on l2_task.
 *
 * @param  ok        the module took the whole image and answered OK.  It is now
 *                   holding a staged image and still running the OLD firmware --
 *                   nothing switches over until ebadge_port_ota_restart().
 * @param  error_no  the module's own error number when !ok, else 0.  Passed
 *                   through rather than translated: the numbers distinguish
 *                   faults that need different responses (a rejected length is
 *                   not a failed flash write), and the port has no better name
 *                   for them than the vendor's.
 */
typedef void (*ebadge_ota_done_cb_t)(bool ok, int error_no);

/** @brief  Bring the OTA path up.  Call once from ebadge_task_init(). */
void ebadge_port_ota_init(void);

/**
 * @brief  Push a whole firmware image at the Wi-Fi module.
 *
 * @param  image    BORROWED.  Must stay valid and unmodified until @p done
 *                  fires -- nothing is copied, because the image is megabytes and
 *                  already sits in PSRAM.
 * @param  len      image length in bytes
 * @param  done     called exactly once on l2_task, possibly a minute later.  May
 *                  be NULL, though a caller with a session to end will want it.
 *
 * @retval 0         accepted; @p done will fire
 * @retval -EINVAL   NULL image or len == 0
 * @retval -EALREADY a push is already in flight
 * @retval -ENOTSUP  built without the Wi-Fi module
 * @retval other     the transport refused to queue it; @p done will NOT fire
 */
int  ebadge_port_ota_push(const uint8_t *image, uint32_t len,
                          ebadge_ota_done_cb_t done);

/**
 * @brief  Restart the module so a staged image becomes the running one.
 *
 * Fire and forget: there is no useful callback, because success is the module
 * disappearing.  Everything it was told is gone afterwards -- SoftAP, TCP server,
 * open sockets -- so callers must have finished with all three first.
 *
 * @retval 0        queued
 * @retval -ENOTSUP built without the Wi-Fi module
 * @retval other    the transport refused to queue it
 */
int  ebadge_port_ota_restart(void);

/** @brief  True between a successful ebadge_port_ota_push() and its callback. */
bool ebadge_port_ota_busy(void);

/**
 * @brief  Bytes handed to the module so far in the current push.
 *
 * Send-side only.  The module acknowledges nothing until the final OK, so this
 * says how much has left this chip, NOT how much has reached its flash.  Reads 0
 * when no push is in flight.
 */
uint32_t ebadge_port_ota_sent(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_OTA_H_ */
