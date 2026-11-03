/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*
 * Bluetooth LE stack bring-up.
 *
 * GAP, the GATT server and the navigation projection service are compiled
 * independently, but none of them starts the controller: the stack is only
 * brought up by gap_start_bt_stack(), which the BLE task calls from its own
 * thread. This file keeps the fixed call order in one place so main() stays
 * readable and the dependency chain cannot be broken by a reordering.
 *
 * The order follows realtek-app/samples/ble_peripheral:
 *   bt_bond_init()         bond storage; must exist before GAP registers its
 *                          bond callbacks
 *   le_gap_init()          GAP parameter block and link table
 *   gap_lib_init()         extended GAP helpers (libgap_utils)
 *   app_ble_gap_init()     device name, pairing parameters, extended advertising
 *   app_ble_service_init() GATT server plus BAS / simple / navigation services
 *   ble_task_init()        event thread; this is what calls gap_start_bt_stack()
 *
 * Advertising starts from the GAP device-state callback once the stack reports
 * GAP_INIT_STATE_STACK_READY, so no explicit start is needed here.
 */

#include <zephyr/kernel.h>
#include "trace.h"
#include "gap.h"
#include "gap_le.h"
#include "bt_bond_api.h"
#include "app_ble_flags.h"
#include "app_ble_gap.h"
#include "app_ble_service.h"
#include "ble_task.h"
#include "ble_init.h"
#include "navi_stream_bridge.h"

void app_ble_init(void)
{
    bt_bond_init();
    le_gap_init(APP_MAX_LINKS);
    gap_lib_init();

    app_ble_gap_init();
    app_ble_service_init();

    /* Attach the frame consumer before the first frame can arrive. */
    navi_stream_bridge_init();

    ble_task_init();

    APP_PRINT_INFO0("app_ble_init: LE stack starting, advertising as BLE_NAVI");
}
