/*
 * app_link_util.c — BLE link table management.
 *
 * Mirrors sdk/src/sample/ota_demo/app_link_util.c LE-link API; BR/EDR slot
 * management lives in src/hmi_rtk_bt/hmi_br_edr/hmi_br_edr_link.c and is not
 * mirrored here.
 */

#include <string.h>
#include <stdlib.h>
#include "trace.h"
#include "os_queue.h"
#include "app_main.h"
#include "app_link_util.h"

T_APP_LE_LINK *app_link_find_le_link_by_conn_id(uint8_t conn_id)
{
    T_APP_LE_LINK *p_link = NULL;

    for (uint8_t i = 0; i < MAX_BLE_LINK_NUM; i++)
    {
        if (app_db.le_link[i].used == true &&
            app_db.le_link[i].conn_id == conn_id)
        {
            p_link = &app_db.le_link[i];
            break;
        }
    }

    return p_link;
}

T_APP_LE_LINK *app_link_alloc_le_link_by_conn_id(uint8_t conn_id)
{
    T_APP_LE_LINK *p_link = NULL;

    for (uint8_t i = 0; i < MAX_BLE_LINK_NUM; i++)
    {
        if (app_db.le_link[i].used == false)
        {
            p_link = &app_db.le_link[i];
            p_link->used    = true;
            p_link->id      = i;
            p_link->conn_id = conn_id;
            break;
        }
    }

    if (p_link == NULL)
    {
        APP_PRINT_ERROR1("app_link_alloc_le_link_by_conn_id: no free slot for conn_id %d",
                         conn_id);
    }
    return p_link;
}

bool app_link_free_le_link(T_APP_LE_LINK *p_link)
{
    if (p_link == NULL || p_link->used != true)
    {
        return false;
    }

    while (p_link->disc_cb_list.count > 0)
    {
        T_LE_DISC_CB_ENTRY *p_entry = os_queue_out(&p_link->disc_cb_list);
        if (p_entry != NULL)
        {
            free(p_entry);
        }
    }

    memset(p_link, 0, sizeof(T_APP_LE_LINK));
    p_link->conn_id = 0xFF;
    return true;
}

T_APP_BR_LINK *app_link_find_br_link(uint8_t *bd_addr)
{
    for (uint8_t i = 0; i < MAX_BR_LINK_NUM; i++)
    {
        if (app_db.br_link[i].used &&
            memcmp(app_db.br_link[i].bd_addr, bd_addr, 6) == 0)
        {
            return &app_db.br_link[i];
        }
    }
    return NULL;
}

T_APP_BR_LINK *app_link_alloc_br_link(uint8_t *bd_addr)
{
    T_APP_BR_LINK *p_link = app_link_find_br_link(bd_addr);
    if (p_link != NULL)
    {
        return p_link;
    }

    for (uint8_t i = 0; i < MAX_BR_LINK_NUM; i++)
    {
        if (!app_db.br_link[i].used)
        {
            memset(&app_db.br_link[i], 0, sizeof(T_APP_BR_LINK));
            memcpy(app_db.br_link[i].bd_addr, bd_addr, 6);
            app_db.br_link[i].used = true;
            app_db.br_link[i].id  = i;
            return &app_db.br_link[i];
        }
    }

    APP_PRINT_ERROR0("app_link_alloc_br_link: no free slot");
    return NULL;
}

bool app_link_free_br_link(T_APP_BR_LINK *p_link)
{
    if (p_link == NULL || !p_link->used)
    {
        return false;
    }
    if (p_link->p_embedded_cmd != NULL)
    {
        free(p_link->p_embedded_cmd);
    }
    memset(p_link, 0, sizeof(T_APP_BR_LINK));
    return true;
}

bool app_link_reg_le_link_disc_cb(uint8_t conn_id, P_FUN_LE_LINK_DISC_CB p_fun_cb)
{
    T_APP_LE_LINK *p_link = app_link_find_le_link_by_conn_id(conn_id);
    if (p_link == NULL)
    {
        APP_PRINT_WARN1("app_link_reg_le_link_disc_cb: conn_id %d not found", conn_id);
        return false;
    }

    T_LE_DISC_CB_ENTRY *p_entry;
    for (uint8_t i = 0; i < p_link->disc_cb_list.count; i++)
    {
        p_entry = os_queue_peek(&p_link->disc_cb_list, i);
        if (p_entry != NULL && p_entry->disc_callback == p_fun_cb)
        {
            return true;
        }
    }

    p_entry = calloc(1, sizeof(T_LE_DISC_CB_ENTRY));
    if (p_entry == NULL)
    {
        return false;
    }
    p_entry->disc_callback = p_fun_cb;
    os_queue_in(&p_link->disc_cb_list, p_entry);
    return true;
}
