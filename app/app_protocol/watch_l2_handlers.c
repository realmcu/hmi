#include "watch_l2_handlers.h"
#include "watch_l2_cmd_ota.h"
#include "watch_l2_cmd_settings.h"
#include "watch_l2_cmd_bind.h"
#include "watch_l2_cmd_notify.h"
#include "watch_l2_cmd_sport.h"
#include "watch_l2_cmd_factory.h"
#include "watch_l2_cmd_control.h"
#include "watch_l2_cmd_log.h"
#include "watch_l2_cmd_xfer.h"
#include "watch_l2_cmd_conn_param.h"

/**
 * @brief  Register every L2 command handler in one place.
 *
 * Called once during app bring-up. Each per-command register() also
 * subscribes to whatever app_event bus events it needs (e.g. xfer and
 * conn_param both listen for EVT_BLE_DISCONNECTED).
 *
 * Ordering caveats
 * ----------------
 *  - watch_l2_register() writes into a plain array indexed by cmd id, so
 *    the ORDER of these calls has no functional effect on L2 dispatch.
 *  - app_event_subscribe() appends to a table walked in registration
 *    order at publish time. The event bus does NOT guarantee delivery
 *    ordering across independent subscribers; today the two subscribers
 *    to EVT_BLE_DISCONNECTED (xfer then conn_param) are independent and
 *    each runs quickly, so reordering is safe. If you ever add a
 *    subscriber whose correctness depends on running "after" another,
 *    encode that dependency INSIDE the subscriber (e.g. via state it
 *    reads back), NOT via the order of these calls.
 */
static int watch_l2_handlers_register(void)
{
    watch_l2_ota_register();
    watch_l2_settings_register();
    watch_l2_bind_register();
    watch_l2_notify_register();
    watch_l2_sport_register();
    watch_l2_factory_register();
    watch_l2_control_register();
    watch_l2_log_register();
    watch_l2_xfer_register();
    watch_l2_conn_param_register();
    return 0;
}

#include "app_module.h"

const app_module_t app_watch_l2_module =
{
    .name  = "watch_l2",
    .init  = watch_l2_handlers_register,
};
