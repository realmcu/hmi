/*
 * app_core.c
 *
 * Single-shot bring-up:
 *
 *   app_core_init() ─┬─ app_event_bus_init()   (create the msgq)
 *                    ├─ for each module in app_modules.c : m->init()
 *                    │      failures are logged and skipped, bring-up
 *                    │      continues so a broken peripheral doesn't
 *                    │      brick the whole watch
 *                    └─ os_task_create(app_task) which runs the event
 *                          dispatch loop forever
 *
 * After app_core_init() returns, main() may exit — app_task keeps the
 * app layer running.
 */

#include "app_core.h"
#include "app_module.h"
#include "app_event.h"
#include "app_log.h"

#include <os_task.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

APP_LOG_MODULE_REGISTER(app_core);

/* Provided by app_modules.c */
extern const app_module_t *const app_module_list[];
extern const size_t              app_module_count;

/* Handle for the single app-layer task. Kept for future stop/inspection. */
static void *s_app_task_handle;

/**
 * @brief  app_task entry — trampoline into the event dispatch loop.
 *
 * OSIF requires @c void(*)(void*) ; app_event_run_forever is @c void(void) .
 * The task never returns; the loop lives inside the event bus so the
 * queue handle and subscription table stay encapsulated there.
 */
static void app_task_body(void *param)
{
    (void)param;
    APP_LOGI("app_task entered");
    app_event_run_forever();
    /* Not reached under normal operation. */
    APP_LOGE("app_task exited — this should never happen");
}

int app_core_init(void)
{
    APP_LOGI("init begin");

    if (app_event_bus_init() != 0)
    {
        APP_LOGE("event bus init failed");
        return -1;
    }

    for (size_t i = 0; i < app_module_count; ++i)
    {
        const app_module_t *m = app_module_list[i];
        if (m == NULL || m->name == NULL)
        {
            continue;
        }
        APP_LOGI("init %s", m->name);
        if (m->init != NULL)
        {
            int rc = m->init();
            if (rc != 0)
            {
                APP_LOGE("init %s failed rc=%d", m->name, rc);
            }
        }
    }

    bool ok = os_task_create(&s_app_task_handle,
                             APP_TASK_NAME,
                             app_task_body,
                             NULL,
                             APP_TASK_STACK_SIZE,
                             APP_TASK_PRIORITY);
    if (!ok)
    {
        APP_LOGE("failed to create %s", APP_TASK_NAME);
        return -2;
    }
    APP_LOGI("%s created (stack=%u, prio=%u)",
             APP_TASK_NAME,
             (unsigned)APP_TASK_STACK_SIZE,
             (unsigned)APP_TASK_PRIORITY);
    return 0;
}
