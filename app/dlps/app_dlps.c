/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <zephyr/devicetree.h>
#include "app_dlps.h"
#include "pm.h"
#include "section.h"
#include "trace.h"
#include <stdio.h>
#include "fmc_api.h"
#include "fmc_api_ext.h"

static uint32_t dlps_bitmap;

RAM_TEXT_SECTION void app_dlps_enable(uint32_t bit)
{
    if (dlps_bitmap & bit)
    {
        APP_PRINT_INFO3("app_dlps_enable: %08x %08x -> %08x", bit, dlps_bitmap,
                         (dlps_bitmap & ~bit));
    }
    dlps_bitmap &= ~bit;
}

RAM_TEXT_SECTION void app_dlps_disable(uint32_t bit)
{
    if ((dlps_bitmap & bit) == 0)
    {
        APP_PRINT_INFO3("app_dlps_disable: %08x %08x -> %08x", bit, dlps_bitmap,
                         (dlps_bitmap | bit));
    }
    dlps_bitmap |= bit;
}
RAM_TEXT_SECTION bool app_dlps_check_callback(void)
{
    static uint32_t dlps_bitmap_pre;
    bool dlps_enter_en = dlps_bitmap == 0;

    if ((dlps_bitmap_pre != dlps_bitmap) && !dlps_enter_en)
    {
        printf("app_dlps_check_callback: dlps_bitmap_pre 0x%x dlps_bitmap 0x%x",
               dlps_bitmap_pre, dlps_bitmap);
    }
    dlps_bitmap_pre = dlps_bitmap;
    return dlps_enter_en;
}

RAM_TEXT_SECTION void app_dlps_enter_callback(void)
{
    fmc_psram_enter_lpm(FMC_SPIC_ID_1, FMC_PSRAM_LPM_HALF_SLEEP_MODE);
    fmc_psram_enter_lpm(FMC_SPIC_ID_3, FMC_PSRAM_LPM_HALF_SLEEP_MODE);
}

RAM_TEXT_SECTION void app_dlps_exit_callback(void)
{
    fmc_psram_exit_lpm(FMC_SPIC_ID_1, FMC_PSRAM_LPM_HALF_SLEEP_MODE);
    fmc_psram_exit_lpm(FMC_SPIC_ID_3, FMC_PSRAM_LPM_HALF_SLEEP_MODE);
}

bool app_dlps_check_enter_bits(uint32_t bit)
{
    return (dlps_bitmap & bit) != 0;
}

void app_dlps_init(void)
{
    if (power_check_cb_register(app_dlps_check_callback) != 0)
    {
        APP_PRINT_ERROR0("app_dlps_init: dlps_check_cb_reg failed");
    }

    power_stage_cb_register(app_dlps_enter_callback, POWER_STAGE_STORE);
    power_stage_cb_register(app_dlps_exit_callback, POWER_STAGE_RESTORE);

#if CONFIG_APP_NANDBOOT
    fmc_psram_register_lpm_func();
#endif
}
