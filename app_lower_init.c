#include <os_sched.h>
#include <stdio.h>
#include <trace.h>
#include "fmc_api.h"
#include "os_mem.h"
#include "os_heap.h"
#include "os_msg.h"
#include "dma_channel.h"
#include "fmc_api_ext.h"
#include <rtl876x_rcc.h>
#include <fmc_api.h>
#include "platform_utils.h"
#include "rtl876x_pinmux.h"
#include "wdg.h"
#include "pm.h"
#include "system_status_api.h"
#include "os_task.h"
#include "cm55_setting.h"
#include "flash_map.h"
#include "mpu.h"
#include <string.h>

extern uint32_t sys_timestamp_get_us(void);

/* Both PSRAM windows are mapped Non-cacheable (attribute 0x44).
 *
 * SPIC1 has been that way all along and app/protocol/port/ebadge_psram_map.h
 * depends on it: the SPI slot buffers (psram1_nc) and the preview frame buffer
 * are GDMA or cross-thread targets that carry no cache maintenance at all, which
 * is only correct because nothing caches them.
 *
 * SPIC3 is the one being brought into line here, and the reason it was NOT
 * already is worth stating because it looks like an omission rather than a
 * setting: nothing in this app ever enabled a cache on it.  ARMv8-M's default
 * memory map declares 0x20000000..0x3FFFFFFF as Normal, Write-Back
 * Write-Allocate, so a window with no MPU region over it is cacheable by
 * default.  Silence was the "on" position.
 *
 * What that costs, said plainly: reads out of SPIC3 get slower by roughly the
 * factor that made SPIC3 attractive in the first place.  On the sibling apps
 * (eBadge_8773g, simple-test) the HoneyGUI lower heap lives at SPIC3_MEM_BASE
 * and their decode throughput is exactly this attribute -- see
 * gui_port_os.c:142 there.  THIS app does not: its lower heap is
 * EB_PSRAM_GUI_LOWER_BASE == SPIC1_MEM_BASE, so SPIC3 was initialised and
 * clocked but unallocated when this region was added, and it changed no current
 * access -- it set the policy that any future allocation there would inherit,
 * which was the whole point of doing it before something got built on the faster
 * behaviour.
 *
 * That future arrived: the Wi-Fi receive cache (1 MB) and the 8711 OTA staging
 * buffer (2 MB) now live at SPIC3_MEM_BASE, carved out in ebadge_psram_map.h,
 * and they inherit this attribute.  Both are memcpy-in / read-back-once staging
 * with no cache maintenance anywhere, so this region is now load-bearing for
 * them exactly as region 2 is for psram1_nc.  Note the ordering that makes it
 * safe: this runs from app_mpu_config(), well before the protocol task exists.
 */
static int app_mpu_config(void)
{
    uint32_t mpu_cfg_rbar = SPIC1_MEM_BASE | (1 << 1); // SH:0, AP:1, XN:0
    uint32_t limit_addr = SPIC1_MEM_BASE + 0x400000; // PXN:0, EN:1
    uint8_t region_num = 2;
    //uint8_t attribute = 0xFF;
    uint8_t attribute = 0x44;

    mpu_config_print();
    mpu_set_region(mpu_cfg_rbar, limit_addr, region_num, attribute, true);

    /* Same 4 MB span and same attribute as SPIC1 above.  4 MB rather than the
     * 256 MB FMC_MAIN3_SIZE claims: that is the controller's addressable range,
     * not the part that is populated -- the sibling apps treat SPIC3 as 4 MB
     * (gui_port_os.c:143, 0x400000) and this mirrors them.  A region that
     * covered unpopulated address space would only make a stray access fault
     * differently, not sooner.
     *
     * Region 4, skipping 3, which is what applications/watch does for its own
     * SPIC3 region -- the low numbers are configured before this runs (see the
     * mpu_config_print() above, which is here to show exactly that), so
     * following a shipping app's choice beats picking the next free-looking
     * index.
     *
     * limit_addr carries the -1 that region 2 above lacks: the hardware forces
     * RLAR[4:0] to 0x1F, so a bare +0x400000 describes a region ending 32 bytes
     * PAST the intended top.  Harmless for region 2, whose overshoot lands in
     * unmapped space between the two windows, but there is no reason to repeat
     * it. */
    mpu_cfg_rbar = SPIC3_MEM_BASE | (1 << 1);
    limit_addr   = SPIC3_MEM_BASE + 0x400000 - 1;
    region_num   = 4;
    attribute    = 0x44;
    mpu_set_region(mpu_cfg_rbar, limit_addr, region_num, attribute, true);

    mpu_config_print();

    {
        // uint32_t mpu_cfg_rbar = SPIC3_MEM_BASE | (1 << 1); // SH:0, AP:1, XN:0
        // uint32_t limit_addr = SPIC3_MEM_BASE + 0x400000; // PXN:0, EN:1
        // uint8_t region_num = 4;
        // uint8_t attribute = 0xAA;
        // mpu_set_region(mpu_cfg_rbar, limit_addr, region_num, attribute, true);
        // mpu_config_print();
    }

    return 0;
}

static void spic0_nor_flash_speed_report(void)
{
#define SECTION_SIZE 1024
    static uint32_t volatile test_buffer[SECTION_SIZE];

    uint32_t color_buf = 0;

    uint32_t total_time = 0;
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t old_stamp = sys_timestamp_get_us();
        memcpy((void *)test_buffer, (void *)(USER_DATA1_ADDR + i * SECTION_SIZE * 4),
               sizeof(uint32_t)*SECTION_SIZE);
        uint32_t new_stamp = sys_timestamp_get_us();
        total_time = total_time + new_stamp - old_stamp;
        for (uint32_t j = 0; j < 1024; j++)
        {
            checksum += test_buffer[j];
        }
    }
    DBG_DIRECT("[Checksum] %u", checksum);

    uint32_t time_ms = total_time / 1000;
    uint32_t time_us = total_time % 1000;

    DBG_DIRECT("[Nor Flash Read Speed Report] SystemCpuClock = %dM; Read 1M byte t=%d.%d ms",
               SystemCpuClock / 1000000, time_ms, time_us);
    DBG_DIRECT("[Nor Flash Read Speed Report] SystemCpuClock = %dM; %dMByte/S",
               SystemCpuClock / 1000000, 1000000 / total_time);

}

static void spic1_psram_speed_report(void)
{
#define SECTION_SIZE 1024
    static uint32_t volatile test_buffer[SECTION_SIZE];

    uint32_t color_buf = 0;

    uint32_t total_time = 0;
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t old_stamp = sys_timestamp_get_us();
        memcpy((void *)test_buffer, (void *)(SPIC1_MEM_BASE + i * SECTION_SIZE * 4),
               sizeof(uint32_t)*SECTION_SIZE);
        uint32_t new_stamp = sys_timestamp_get_us();
        total_time = total_time + new_stamp - old_stamp;
        for (uint32_t j = 0; j < 1024; j++)
        {
            checksum += test_buffer[j];
        }
    }
    DBG_DIRECT("[Checksum] %u", checksum);

    uint32_t time_ms = total_time / 1000;
    uint32_t time_us = total_time % 1000;

    DBG_DIRECT("[SPIC1 PSRAM Read Speed Report] SystemCpuClock = %dM; Read 1M byte t=%d.%d ms",
               SystemCpuClock / 1000000, time_ms, time_us);
    DBG_DIRECT("[SPIC1 PSRAM Read Speed Report] SystemCpuClock = %dM; %dMByte/S",
               SystemCpuClock / 1000000, 1000000 / total_time);
}

static void spic3_psram_speed_report(void)
{
#define SECTION_SIZE 1024
    static uint32_t volatile test_buffer[SECTION_SIZE];

    uint32_t color_buf = 0;

    uint32_t total_time = 0;
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t old_stamp = sys_timestamp_get_us();
        memcpy((void *)test_buffer, (void *)(SPIC3_MEM_BASE + i * SECTION_SIZE * 4),
               sizeof(uint32_t)*SECTION_SIZE);
        uint32_t new_stamp = sys_timestamp_get_us();
        total_time = total_time + new_stamp - old_stamp;
        for (uint32_t j = 0; j < 1024; j++)
        {
            checksum += test_buffer[j];
        }
    }
    DBG_DIRECT("[Checksum] %u", checksum);

    uint32_t time_ms = total_time / 1000;
    uint32_t time_us = total_time % 1000;

    DBG_DIRECT("[SPIC3 PSRAM Read Speed Report] SystemCpuClock = %dM; Read 1M byte t=%d.%d ms",
               SystemCpuClock / 1000000, time_ms, time_us);
    DBG_DIRECT("[SPIC3 PSRAM Read Speed Report] SystemCpuClock = %dM; %dMByte/S",
               SystemCpuClock / 1000000, 1000000 / total_time);
}




void app_system_lower_init(void)
{
    app_mpu_config();
    sys_hall_auto_sleep_in_idle(false);

    uint32_t cpu_freq;
    int32_t ret = pm_cpu_freq_set(200, &cpu_freq);
    if (ret != 0)
    {
        DBG_DIRECT("cpu freq config CLK_100MHZ fail ret %x real freq %dMHz", ret, cpu_freq);
    }
    else
    {
        DBG_DIRECT("cpu freq %dMHz ", cpu_freq);
    }

    uint32_t spic0_freq = 0;

    if (fmc_flash_try_high_speed_mode(FMC_SPIC_ID_0, FMC_FLASH_NOR_4_BIT_MODE))
    {
        DBG_DIRECT("flash switch 4bit success");
    }
    else
    {
        DBG_DIRECT("flash switch 4bit fail");
    }
    if (fmc_flash_nor_clock_switch(FMC_SPIC_ID_0, 200, &spic0_freq))
    {
        DBG_DIRECT("set flash clock 200M success");
    }
    else
    {
        DBG_DIRECT("set flash clock 200M fail");
    }


    if (fmc_psram_winbond_opi_init(FMC_SPIC_ID_1))
    {
        DBG_DIRECT("WB OPI psram init success!");
    }
    else
    {
        DBG_DIRECT("WB OPI psram init fail!");
    }

    extern bool fmc_psram_clock_switch(FMC_SPIC_ID spic_id, uint32_t required_mhz,
                                       uint32_t *actual_mhz);

    uint32_t spic1_freq = 0;
    if (fmc_psram_clock_switch(FMC_SPIC_ID_1, 280, &spic1_freq))
    {
        DBG_DIRECT("WB OPI psram switch to 280MHz success!");
    }
    else
    {
        DBG_DIRECT("WB OPI psram switch to 280MHz fail!");
    }

    if (fmc_psram_winbond_opi_init(FMC_SPIC_ID_3))
    {
        DBG_DIRECT("APM OPI psram init success!");
    }
    else
    {
        DBG_DIRECT("APM OPI psram init fail!");
    }

    uint32_t spic3_freq = 0;
    /* &spic3_freq, not &spic1_freq -- it was the latter, so the "actual %d" in
     * both branches below printed the 0 this variable was initialised to and
     * the SPIC1 figure above got silently overwritten.  Worth being right now
     * that the MPU region over this window is the thing under test: the clock
     * is the other half of how fast SPIC3 reads, and a log that always says 0
     * cannot rule it out. */
    if (fmc_psram_clock_switch(FMC_SPIC_ID_3, 280, &spic3_freq))
    {
        DBG_DIRECT("APM OPI psram switch to %d MHz success! actual %d", 280, spic3_freq);
    }
    else
    {
        DBG_DIRECT("APM OPI psram switch to %dMHz fail! actual %d", 280, spic3_freq);
    }

    return;
}




