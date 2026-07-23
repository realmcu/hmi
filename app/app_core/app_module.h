#ifndef __APP_MODULE_H__
#define __APP_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   App layer module contract.
 *
 * Every app-layer sub-module (health / ble / media / ...) defines one
 * @c const @c app_module_t and lists it in @c app_modules.c . During
 * bring-up @c app_core walks the list once and calls @c init on every
 * module in list order. That's it — modules do all their setup in
 * @c init , including subscribing to events and (rarely) launching any
 * private worker task.
 *
 * Threading model
 * ---------------
 * The app layer runs on a SINGLE task, @c app_task , created by
 * @c app_core_init() after every module's @c init has returned.
 * Consequences:
 *
 *  - @c init runs on the CALLER thread (main), before @c app_task exists.
 *    It MUST NOT block; it should complete quickly and return.
 *  - After @c init , every event-bus callback the module subscribed to
 *    runs on @c app_task , serially with every other module's callback.
 *    Callbacks therefore MUST NOT block — a stuck callback stalls the
 *    whole app layer.
 *  - Modules do NOT start their own tasks by default. If a module truly
 *    needs long-running work (OTA writes, NN inference) it may create a
 *    dedicated internal task, but that must be an explicit design
 *    decision inside the module — not the default path.
 *
 * init contract
 * -------------
 *  - Subscribe to events, allocate resources, register shell commands.
 *  - MUST NOT @c app_event_publish : the dispatch loop is not running
 *    yet, and even if it were, later modules haven't subscribed. Save
 *    "system ready" style broadcasts for after bring-up.
 *  - Returns 0 on success, non-zero on failure. Failure is logged; the
 *    overall bring-up continues so the watch stays partially usable when
 *    a peripheral fails.
 */
typedef struct app_module
{
    const char *name;              /* short identifier, e.g. "health" */
    int (*init)(void);
} app_module_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_MODULE_H__ */
