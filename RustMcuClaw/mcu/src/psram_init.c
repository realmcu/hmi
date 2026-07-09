/**
 * @file psram_init.c
 * @brief PSRAM initialization for RustMcuClaw MCU app.
 *
 * Initialises Winbond OPI PSRAM on SPIC1 (psram0) and SPIC3 (psram1),
 * and configures MPU regions so that the CPU can read/write PSRAM.
 *
 * Ported from the rust_ffi_demo/watch lower-init flow.
 */

#include <zephyr/devicetree.h>

#include <stdint.h>

#include <trace.h>

#include "address_map.h"
#include "clk_mgr.h"
#include "dma_channel.h"
#include "fmc_api.h"
#include "fmc_api_ext.h"
#include "mpu.h"
#include "platform_utils.h"
#include "psram_init.h"
#include "section.h"

#include <rtl876x_rcc.h>

#define ZEPHYR_DMA_CHANNEL_MASK (BIT10 | BIT11 | BIT12 | BIT13 | BIT14 | BIT15)

static T_CLK_USER_HANDLE clk_user_gui;

static void psram_mpu_config(void)
{
    /* PSRAM0 via SPIC1 - 4 MB, write-through (0xAA) */
    uint32_t rbar = SPIC1_MEM_BASE | (1 << 1); /* SH:0, AP:1, XN:0 */
    uint32_t limit = SPIC1_MEM_BASE + 0x400000 - 1;
    mpu_set_region(rbar, limit, 2, 0xAA, true);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(psram1), okay)
    /* PSRAM1 via SPIC3 - size from DTS, write-through (0xAA) */
    rbar = SPIC3_MEM_BASE | (1 << 1);
    limit = SPIC3_MEM_BASE + DT_REG_SIZE(DT_NODELABEL(psram1_for_mcu)) - 1;
    mpu_set_region(rbar, limit, 4, 0xAA, true);
#endif
}

static void app_io_resource_cfg(void)
{
    /* Mark DMA channels already allocated to Zephyr and DSP */
    uint16_t dma_channel_masked = GDMA_channel_get_active_mask();
    dma_channel_cfg(dma_channel_masked | ZEPHYR_DMA_CHANNEL_MASK);

    /* HW timers available for hw_timer_create */
    extern void hw_timer_channel_cfg(uint16_t hw_timer_mask);
    hw_timer_channel_cfg(BIT2 | BIT3);
}

void psram_init(void)
{
    psram_mpu_config();

    /* ---- Flash high-speed 4-bit mode ---- */
    if (fmc_flash_try_high_speed_mode(FMC_SPIC_ID_0, FMC_FLASH_NOR_4_BIT_MODE))
    {
        DBG_DIRECT("flash switch 4bit success");
    }
    else
    {
        DBG_DIRECT("flash switch 4bit fail");
    }

#if DT_NODE_HAS_STATUS(DT_NODELABEL(psram0), okay)
    if (fmc_psram_winbond_opi_init(FMC_SPIC_ID_1))
    {
        DBG_DIRECT("psram0 (SPIC1) init success!");
    }
    else
    {
        DBG_DIRECT("psram0 (SPIC1) init FAIL!");
    }
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(psram1), okay)
    if (fmc_psram_winbond_opi_init(FMC_SPIC_ID_3))
    {
        DBG_DIRECT("psram1 (SPIC3) init success!");
    }
    else
    {
        DBG_DIRECT("psram1 (SPIC3) init FAIL!");
    }
#endif

    /* ---- Clock manager init + high performance ---- */
    clk_mgr_init();
    app_io_resource_cfg();

    U_CLK_BITMAP bitmap;
    bitmap.data = BIT(T_CLK_TYPE_CPU) | BIT(T_CLK_TYPE_SPIC0) | BIT(T_CLK_TYPE_SPIC1);
    clk_user_gui = clk_mgr_user_create("gui", bitmap);
    clk_mgr_set_high_performance(clk_user_gui);
}
