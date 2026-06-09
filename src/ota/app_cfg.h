/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_CFG_H_
#define _APP_CFG_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" { /* __cplusplus */
#endif

typedef struct
{
    uint8_t bud_local_addr[6];
} T_APP_CFG_NV;


extern T_APP_CFG_NV app_cfg_nv;

#ifdef __cplusplus
} /* __cplusplus */
#endif

#endif /*_AU_CFG_H_*/
