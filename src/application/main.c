/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <stdio.h>
#include <trace.h>
#include "os_sched.h"
#include "app_panel_init.h"
#include "app_lower_init.h"
#ifdef ENABLE_HONEYGUI
#include "gui_server.h"
#endif

#include "wdg.h"
#include "file_db.h"
#include "string.h"
#include "file_db_port_nor_flash.h"
#include "fmc_api.h"

#if 0  // streaming
#include "gui_stream.h"
#define STREAM_DB  (void *)(0x4000000 + 0x300000)
#define STREAM_SIZE  0x100000u
#define MAX_FRAME       (50u * 1024u)   /* per-buffer cap (>> any real frame) */
#define POOL_BUFS       6u              /* FIFO depth per stream              */
// static uint8_t       s_pool_duck[MAX_FRAME * POOL_BUFS + 64u];
typedef struct
{
    // avi_info_t       info;
    stp_transport_t *tp;
    uint8_t         *pool;
    uint32_t         pool_size;
    uint32_t         interval_ms;
    const char      *label;
    volatile bool    running;
} demo_stream_t;
#endif



// #define MOUNT_DB  (void *)(0x4000000 + 0x300000)
#define MOUNT_DB  (void *)(0x240F400 + 0x700000u)
static uint8_t s_dir_cache[4096];   /* >= align_up(dir_bytes, sector_size) */

int fdb_flash_nor_read(uint32_t addr, void *data, uint32_t len)
{
    memcpy(data, (void *)addr, len);
    return 0;
}
int fdb_flash_nor_prog_sector(uint32_t abs_addr, const void *buf, uint32_t len)
{
    int rc = fmc_flash_nor_write(abs_addr, buf, len);
    if (!rc)
    {
        APP_PRINT_INFO4("fdb fmc_flash_nor_write  0x%x 0x%x %d %d", abs_addr, buf, len, rc);
    }
    return  rc == 1 ? 0 : -1;
}
int fdb_flash_nor_erase_sector(uint32_t abs_addr_sector_aligned)
{
    int rc = fmc_flash_nor_erase(abs_addr_sector_aligned, FMC_FLASH_NOR_ERASE_SECTOR);
    if (!rc)
    {
        APP_PRINT_INFO2("fdb fmc_flash_nor_erase  0x%x %d", abs_addr_sector_aligned, rc);
    }
    return  rc == 1 ? 0 : -1;
}

int main(void)
{
    APP_PRINT_INFO1("main function line = %d!", __LINE__);

    system_lower_init();

    extern void rtk_lcd_hal_init(void);
    rtk_lcd_hal_init();


    wdg_kick();

#if 1  // flash file store
//    fdb_flash_nor_erase_sector((uint32_t)MOUNT_DB);
    int fmc_rc = fmc_flash_nor_read((uint32_t)MOUNT_DB, s_dir_cache, sizeof(s_dir_cache));
    APP_PRINT_INFO1("fdb fmc_flash_nor_read rc=%d", fmc_rc);
    fdb_nor_cfg_t s_cfg =
    {
        .read = (fdb_nor_hal_read_t)fdb_flash_nor_read,
        .program = (fdb_nor_hal_write_t)fdb_flash_nor_prog_sector,     /* page-program；len 受 page_size 限制 */
        .erase_sector = (fdb_nor_hal_erase_t)fdb_flash_nor_erase_sector,/* 擦一个扇区，参数必须扇区对齐       */
        .base_addr = (uint32_t)MOUNT_DB,   /* file_db 区域在芯片上的起始地址      */
        .region_size = 0x200000, /* file_db 区域大小                    */
        .sector_size = 4096, /* 例如 4096                           */
        .page_size = 256,   /* 例如 256；< sector_size             */
        .dir_bytes            = 4096,                 /* == data_offset(FDB_DATA_ALIGN=4096 时) */
        .dir_cache            = s_dir_cache,
        .dir_cache_capacity   = sizeof(s_dir_cache),
    };

    fdb_port_nor_setup(&s_cfg);
    int fdb_rc = fdb_init(fdb_port_nor_get_ops());
    APP_PRINT_INFO1("fdb_init rc=%d", fdb_rc);

    fdb_rc = fdb_mount();
    APP_PRINT_INFO1("fdb_mount rc=%d", fdb_rc);
    if (fdb_rc != FDB_OK)
    {
        fdb_rc = fdb_format();
        APP_PRINT_INFO1("fdb_format rc=%d", fdb_rc);
        if (fdb_rc != FDB_OK)
        {
            APP_PRINT_ERROR1("fdb format failed, rc=%d", fdb_rc);
            // 错误处理
        }
        else
        {
            fdb_rc = fdb_mount();   // 重新挂载,载入 dir[]
            APP_PRINT_INFO1("fdb_mount after format rc=%d", fdb_rc);
            if (fdb_rc != FDB_OK)
            {
                APP_PRINT_ERROR1("fdb remount after format failed, rc=%d", fdb_rc);
            }
        }
    }
    APP_PRINT_INFO2("fdb init done, rc=%d mounted=%d", fdb_rc, fdb_is_mounted());

    fdb_dump_super();
    fdb_dump_usage();

    // read dir
    uint32_t file_num = 0;
    void **file_array = NULL;
    fdb_get_file_count(&file_num);
    if (file_num)
    {
        file_array = malloc(sizeof(void *) * file_num);
        for (uint32_t i = 0; i < file_num; ++i)
        {
            uintptr_t addr;
            uint32_t  size, id;
            if (fdb_get_file_addr(i, &addr, &size, &id) != FDB_OK) { break; }

            APP_PRINT_INFO4("[%u] id=0x%08X  addr=0x%08lX  size=%u\n",
                            i, id, (unsigned long)addr, size);
            file_array[i] = (void *)addr;
        }
        free(file_array);
    }
    // construct resouce list
    extern uint8_t mainface_list_init(void **data_list, uint32_t n);
    mainface_list_init(file_array, file_num);
#endif

#if 0  // streaming

    extern demo_stream_t s_stream_bt;
    s_stream_bt.tp          = NULL;
    s_stream_bt.pool        = STREAM_DB;
    s_stream_bt.pool_size   = STREAM_SIZE;
    s_stream_bt.label       = NULL;
    s_stream_bt.interval_ms = 46;

    static const stp_class_cfg_t classes[] =
    {
        { .buf_size = MAX_FRAME, .buf_count = POOL_BUFS },
    };
    stp_config_t cfg;
    stp_config_default(&cfg);
    cfg.pool        = s_stream_bt.pool;
    cfg.pool_size   = s_stream_bt.pool_size;
    cfg.align       = 4;
    cfg.classes     = classes;
    cfg.class_count = 1;
    cfg.drop_mode   = STP_DROP_NONE;

    s_stream_bt.tp = stp_create(&cfg);
    if (!s_stream_bt.tp)
    {
        DBG_DIRECT("stream demo: stp_create failed\n");
        return NULL;
    }


#endif





#ifdef ENABLE_HONEYGUI
    app_task_init();
    gui_set_keep_active_time(1000000);
#ifdef __cplusplus
    {
        typedef void PROC();
        extern const unsigned long SHT$$INIT_ARRAY$$Base[];
        extern const unsigned long SHT$$INIT_ARRAY$$Limit[];

        const unsigned long *base = SHT$$INIT_ARRAY$$Base;
        const unsigned long *lim  = SHT$$INIT_ARRAY$$Limit;

        for (; base != lim; base++)
        {
            PROC *proc = (PROC *)((const char *)base + *base);
            (*proc)();
        }
    }
#endif
#endif

    extern void hmi_bt_task_init(void);
    extern void hmi_proto_task_init(void);
    extern void hmi_l2_handlers_register(void);
    hmi_bt_task_init();
    hmi_proto_task_init();
    hmi_l2_handlers_register();

    os_sched_start();
    while (1);

}
