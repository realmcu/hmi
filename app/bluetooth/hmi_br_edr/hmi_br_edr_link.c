/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#include <string.h>
#include "hmi_br_edr_link.h"

T_BR_EDR_APP_DB hmi_br_edr_app_db;

T_BR_EDR_LINK *hmi_br_edr_find_link(uint8_t *bd_addr)
{
    if (bd_addr == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < MAX_BR_LINK_NUM; i++)
    {
        if (hmi_br_edr_app_db.br_link[i].used &&
            !memcmp(hmi_br_edr_app_db.br_link[i].bd_addr, bd_addr, 6))
        {
            return &hmi_br_edr_app_db.br_link[i];
        }
    }

    return NULL;
}

T_BR_EDR_LINK *hmi_br_edr_alloc_link(uint8_t *bd_addr)
{
    if (bd_addr == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < MAX_BR_LINK_NUM; i++)
    {
        if (!hmi_br_edr_app_db.br_link[i].used)
        {
            T_BR_EDR_LINK *p_link = &hmi_br_edr_app_db.br_link[i];
            memcpy(p_link->bd_addr, bd_addr, 6);
            p_link->used = true;
            p_link->id   = i;
            return p_link;
        }
    }

    return NULL;
}

bool hmi_br_edr_free_link(T_BR_EDR_LINK *p_link)
{
    if (p_link != NULL && p_link->used)
    {
        memset(p_link, 0, sizeof(T_BR_EDR_LINK));
        return true;
    }

    return false;
}
