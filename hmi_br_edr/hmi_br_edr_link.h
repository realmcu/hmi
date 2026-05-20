/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation. All rights reserved.
 */

#ifndef _HMI_BR_EDR_LINK_H_
#define _HMI_BR_EDR_LINK_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BR_LINK_NUM  2

typedef struct
{
    uint8_t  bd_addr[6];
    bool     used;
    uint8_t  id;
} T_BR_EDR_LINK;

typedef struct
{
    T_BR_EDR_LINK br_link[MAX_BR_LINK_NUM];
} T_BR_EDR_APP_DB;

extern T_BR_EDR_APP_DB hmi_br_edr_app_db;

T_BR_EDR_LINK *hmi_br_edr_find_link(uint8_t *bd_addr);
T_BR_EDR_LINK *hmi_br_edr_alloc_link(uint8_t *bd_addr);
bool           hmi_br_edr_free_link(T_BR_EDR_LINK *p_link);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_BR_EDR_LINK_H_ */
