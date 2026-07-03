/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*
 * BLE GAP wrappers expected by ota_demo modules.
 *
 * Only the symbols actually referenced from app_ota.c are implemented.
 */

#include "trace.h"
#include "gap_conn_le.h"
#include "app_main.h"
#include "app_link_util.h"
#include "app_ble_gap.h"

bool app_ble_gap_disconnect(T_APP_LE_LINK *p_link, T_LE_LOCAL_DISC_CAUSE disc_cause)
{
    if (p_link == NULL)
    {
        return false;
    }

    APP_PRINT_TRACE2("app_ble_gap_disconnect: conn_id %d, disc_cause %d",
                     p_link->conn_id, disc_cause);

    if (le_disconnect(p_link->conn_id) == GAP_CAUSE_SUCCESS)
    {
        p_link->local_disc_cause = disc_cause;
        return true;
    }
    return false;
}
