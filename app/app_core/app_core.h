#ifndef __APP_CORE_H__
#define __APP_CORE_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_core.h
 * @brief App-layer entry point.
 *
 * The app layer runs on a single dedicated task ("app_task") created by
 * @c app_core_init() . That task owns the event-bus dispatch loop, and
 * every module's event callback runs on it. Callbacks therefore MUST NOT
 * block — a stuck callback stalls the whole app layer. Modules that need
 * long-running work (OTA writes, NN inference) should keep those in a
 * dedicated per-module task; the shared @c app_task stays for event-loop
 * work only.
 */

/** app_task stack size, bytes. Sized for shared use by every module's
 *  event callback; adjust once real usage is measured. */
#define APP_TASK_STACK_SIZE     4096u

/** app_task priority (Realtek OSIF convention: higher value = higher
 *  priority; 3 is a typical mid-range user task priority). */
#define APP_TASK_PRIORITY       3u

/** app_task display name for logs / shell. */
#define APP_TASK_NAME           "app_task"

/**
 * @brief  Bring up the app layer end-to-end.
 *
 * Runs on the caller thread (main). One shot, does everything:
 *   1. initialise the event bus (create the underlying message queue);
 *   2. walk every module in the registry and call its @c init callback;
 *   3. spin up @c app_task , which owns the event dispatch loop from
 *      here on.
 *
 * After this returns, the caller (main) can safely exit — @c app_task
 * keeps the app layer running.
 *
 * @return 0 on success. Currently never fails end-to-end; individual
 *         module @c init failures are logged and skipped.
 */
int app_core_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CORE_H__ */
