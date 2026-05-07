/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <os_sched.h>
#include <stdio.h>
#include <trace.h>
#include "fmc_api.h"
#include "os_mem.h"
#include "os_heap.h"
#include "os_msg.h"
#include "mem_config.h"
#include "dma_channel.h"
#include "fmc_api_ext.h"
#include <rtl876x_rcc.h>
#include <fmc_api.h>
#include "platform_utils.h"
#include "rtl876x_pinmux.h"
#include "wdg.h"
#include "pm.h"
#include "board.h"
#include "system_status_api.h"

#if (SUPPORT_ACCESS_SHM == 1)
#include "os_heap.h"

void shm_data_copy(void)
{
#if defined(__ARMCC_VERSION)
    extern unsigned int Load$$SHARE_RAM_DATA$$RW$$Base;
    extern unsigned int Image$$SHARE_RAM_DATA$$RW$$Base;
    extern unsigned int Image$$SHARE_RAM_DATA$$RW$$Length;
    extern unsigned int Image$$SHARE_RAM_DATA$$ZI$$Base;
    extern unsigned int Image$$SHARE_RAM_DATA$$ZI$$Length;

    uint32_t load_addr = (uint32_t)&Load$$SHARE_RAM_DATA$$RW$$Base;
    uint32_t dest_addr = (uint32_t)&Image$$SHARE_RAM_DATA$$RW$$Base;
    uint32_t len = (uint32_t)&Image$$SHARE_RAM_DATA$$RW$$Length;
    memcpy((uint8_t *)dest_addr, (uint8_t *)load_addr, len);

    dest_addr = (uint32_t)&Image$$SHARE_RAM_DATA$$ZI$$Base;
    len = (uint32_t)&Image$$SHARE_RAM_DATA$$ZI$$Length;
    memset((uint8_t *)dest_addr, 0, len);

    extern unsigned int Load$$SHARE_RAM_DATA$$RO$$Base;
    extern unsigned int Image$$SHARE_RAM_DATA$$RO$$Base;
    extern unsigned int Image$$SHARE_RAM_DATA$$RO$$Length;

    load_addr = (uint32_t)&Load$$SHARE_RAM_DATA$$RO$$Base;
    dest_addr = (uint32_t)&Image$$SHARE_RAM_DATA$$RO$$Base;
    len = (uint32_t)&Image$$SHARE_RAM_DATA$$RO$$Length;
    memcpy((uint8_t *)dest_addr, (uint8_t *)load_addr, len);
#elif defined (__GNUC__)
    extern unsigned int *__share_ram_load_addr__;
    extern unsigned int *__share_ram_dst_addr__;
    extern unsigned int *__share_ram_code_length__;
    memcpy((uint32_t)&__share_ram_dst_addr__,
           (uint32_t)&__share_ram_load_addr__,
           (uint32_t)&__share_ram_code_length__);
#else
#error "Unsupported compiler"
#endif
}

void ram_config()
{
    extern void sys_hall_set_dsp_share_memory_80k(bool is_off_ram);
    sys_hall_set_dsp_share_memory_80k(false);

#if defined TARGET_RTL8773E
#define DSP_SHM_GLOBAL_ADDR    0x300000
#else
#define DSP_SHM_GLOBAL_ADDR    0x300000
#endif

#define DSP_SHM_HEAP_ADDR      DSP_SHM_GLOBAL_ADDR + DSP_SHM_GLOBAL_SIZE

    heap_shm_init(DSP_SHM_HEAP_ADDR, DSP_SHM_HEAP_SIZE);
    heap_shm_set(DSP_SHM_GLOBAL_ADDR, DSP_SHM_TOTAl_SIZE, 0);
    shm_data_copy();
}
#endif

void system_lower_init(void)
{
    sys_hall_auto_sleep_in_idle(false);
    ram_config();

    uint32_t cpu_freq;
    int32_t ret = pm_cpu_freq_set(100, &cpu_freq);
    if (ret != 0)
    {
        APP_PRINT_INFO2("cpu freq config CLK_100MHZ fail ret %x real freq %dMHz", ret, cpu_freq);
    }
    else
    {
        APP_PRINT_INFO1("cpu freq %dMHz ", cpu_freq);
    }
    fmc_flash_set_seq_trans(FMC_SPIC_ID_0, true);
    uint32_t spic0_freq = 0;
    fmc_flash_nor_clock_switch(FMC_SPIC_ID_0, 80, &spic0_freq);
    APP_PRINT_INFO2("APP COMPILE TIME: [%s - %s]", TRACE_STRING(__DATE__), TRACE_STRING(__TIME__));


    if (fmc_flash_try_high_speed_mode(FMC_SPIC_ID_0, FMC_FLASH_NOR_4_BIT_MODE))
    {
        APP_PRINT_INFO0("flash switch 4bit success");
    }
    else
    {
        APP_PRINT_INFO0("flash switch 4bit fail");
    }
    if (fmc_flash_nor_clock_switch(FMC_SPIC_ID_0, 160, &spic0_freq))
    {
        APP_PRINT_INFO0("set flash clock 160M success");
    }
    else
    {
        APP_PRINT_INFO0("set flash clock 160M fail");
    }
    if (fmc_psram_winbond_opi_init(FMC_SPIC_ID_1))
    {
        APP_PRINT_INFO0("WB OPI psram init success!");
    }
    else
    {
        APP_PRINT_INFO0("WB OPI psram init fail!");
    }
    extern bool fmc_psram_clock_switch(FMC_SPIC_ID spic_id, uint32_t required_mhz,
                                       uint32_t *actual_mhz);
    if (fmc_psram_clock_switch(FMC_SPIC_ID_1, 160, &spic0_freq))
    {
        APP_PRINT_INFO0("WB OPI psram switch to 160MHz success!");
    }
    else
    {
        APP_PRINT_INFO0("WB OPI psram switch to 160MHz fail!");
    }
}

