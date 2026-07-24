/*
 * Glue between the ported OTA code and eBadge_8773g's BLE stack.
 * Implements the small link/disconnect surface the sample framework used to provide
 * (app_db, app_cfg_nv, app_link_*, app_ble_gap_disconnect) on top of the SDK GAP API.
 */
#include <string.h>
#include <stdbool.h>
#include "trace.h"
#include "gap_conn_le.h"
#include "app_main.h"
#include "app_cfg.h"
#include "app_ble_gap.h"
#include "app_ota_service.h"

/* Globals the OTA code expects (see compat app_main.h / app_cfg.h). */
T_APP_DB     app_db;
T_APP_CFG_NV app_cfg_nv;

T_APP_LE_LINK *app_link_find_le_link_by_conn_id(uint8_t conn_id)
{
    for (uint8_t i = 0; i < MAX_BLE_LINK_NUM; i++)
    {
        if (app_db.le_link[i].used && app_db.le_link[i].conn_id == conn_id)
        {
            return &app_db.le_link[i];
        }
    }
    return NULL;
}

static T_APP_LE_LINK *ota_glue_link_find_or_alloc(uint8_t conn_id)
{
    T_APP_LE_LINK *p_link = app_link_find_le_link_by_conn_id(conn_id);
    if (p_link != NULL)
    {
        return p_link;
    }
    for (uint8_t i = 0; i < MAX_BLE_LINK_NUM; i++)
    {
        if (!app_db.le_link[i].used)
        {
            memset(&app_db.le_link[i], 0, sizeof(T_APP_LE_LINK));
            app_db.le_link[i].used    = 1;
            app_db.le_link[i].conn_id = conn_id;
            return &app_db.le_link[i];
        }
    }
    return NULL;
}

bool app_link_reg_le_link_disc_cb(uint8_t conn_id, P_FUN_LE_LINK_DISC_CB p_fun_cb)
{
    T_APP_LE_LINK *p_link = ota_glue_link_find_or_alloc(conn_id);
    if (p_link == NULL)
    {
        return false;
    }
    p_link->disc_callback = p_fun_cb;
    return true;
}

bool app_ble_gap_disconnect(T_APP_LE_LINK *p_link, T_LE_LOCAL_DISC_CAUSE disc_cause)
{
    if (p_link == NULL)
    {
        return false;
    }
    p_link->local_disc_cause = (uint8_t)disc_cause;
    return (le_disconnect(p_link->conn_id) == GAP_CAUSE_SUCCESS);
}

/*==================== hooks driven by eBadge's GAP conn-state handler ====================*/

void app_ota_glue_link_connected(uint8_t conn_id, uint16_t conn_handle, uint8_t *bd_addr)
{
    T_APP_LE_LINK *p_link = ota_glue_link_find_or_alloc(conn_id);
    if (p_link == NULL)
    {
        APP_PRINT_ERROR1("app_ota_glue_link_connected: no free link slot, conn_id %d", conn_id);
        return;
    }
    p_link->conn_handle = conn_handle;
    if (bd_addr != NULL)
    {
        memcpy(p_link->bd_addr, bd_addr, 6);
    }
}

void app_ota_glue_link_disconnected(uint8_t conn_id, uint16_t disc_cause)
{
    T_APP_LE_LINK *p_link = app_link_find_le_link_by_conn_id(conn_id);
    if (p_link == NULL)
    {
        return;
    }
    if (p_link->disc_callback != NULL)
    {
        p_link->disc_callback(conn_id, p_link->local_disc_cause, disc_cause);
    }
    memset(p_link, 0, sizeof(T_APP_LE_LINK));
}
