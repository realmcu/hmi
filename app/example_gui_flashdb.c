/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include "guidef.h"
#include "gui_obj.h"
#include "gui_api.h"
#include "gui_components_init.h"
#include "gui_obj_event.h"
#include "trace.h"

#include "flashdb.h"
#include "os_sync.h"
#include "ui_common.h"          /* TEAR_BF_KEY_PREFIX -- see bf_boot_enum_cb() */

static struct fdb_kvdb s_kvdb = {0};
static struct fdb_bf   s_bf   = {0};
fdb_kvdb_t app_get_kvdb(void) { return &s_kvdb; }
fdb_bf_t   app_get_bf(void)   { return &s_bf;   }

static void *s_db_mutex = NULL;
static void db_lock(fdb_db_t db)
{
    (void)db;
    os_mutex_take(s_db_mutex, 0xFFFFFFFF);
}

static void db_unlock(fdb_db_t db)
{
    (void)db;
    os_mutex_give(s_db_mutex);
}
/* Tear previews found by the boot scan.  They are bare thumbnails with no
 * PACKET_HEADER_T, so they cannot go through mainface_list_init(); the scan
 * collects them and they are appended as SRC_TEARBADGE entries afterwards. */
#define BF_BOOT_MAX_TEARS       FDB_BF_MAX_ENTRIES
static void *s_tear_info[BF_BOOT_MAX_TEARS * 2];   /* xip_addr, size pairs */
static uint32_t s_tear_num;

static bool bf_boot_enum_cb(const char *key, const struct fdb_bf_dirent *ent,
                            uint32_t xip_addr, void *arg)
{
    void **array = (void **)(arg);
    uint32_t n = (uint32_t)array[0];

    if (strncmp(key, TEAR_BF_KEY_PREFIX, strlen(TEAR_BF_KEY_PREFIX)) == 0)
    {
        if (s_tear_num < BF_BOOT_MAX_TEARS)
        {
            s_tear_info[2 * s_tear_num]     = (void *)xip_addr;
            s_tear_info[2 * s_tear_num + 1] = (void *)(ent->size);
            s_tear_num++;
        }
        printf("[bf] n=%u   '%-24s'  size=%-8u  xip=0x%08X  (tear preview)\n",
               n, key, ent->size, xip_addr);
        return false;
    }

    array[2 * n + 1] = (void *)xip_addr;
    array[2 * n + 2] = (void *)(ent->size);
    array[0] = (void *)(n + 1);
    printf("[bf] n=%u   '%-24s'  size=%-8u  xip=0x%08X  flags=0x%08X\n",
           n, key, ent->size, xip_addr, ent->flags);
    return false;   /* return true to stop early */
}



int flashdb_prepare(void)
{
    fdb_err_t rc;

    /* 1. FAL is initialised inside fdb_kvdb_init when FDB_USING_FAL_MODE is set.
     *    If you call fal_init() explicitly elsewhere, that is fine too. */
    if (os_mutex_create(&s_db_mutex) == true)
    {
        printf("flash db os_mutex_create\n");
    }
    /* 2. KVDB — "env" is the logical name, "fdb_kvdb1" is the FAL partition */
    fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_LOCK, (void *)db_lock);
    fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)db_unlock);

    extern void WDG_Kick_Core(void);
    WDG_Kick_Core();
    rc = fdb_kvdb_init(&s_kvdb, "env", "fdb_kvdb1", NULL, NULL);
    printf("[db] kvdb init rc=%d", (int)rc);
    if (rc != FDB_NO_ERR)
    {
        printf("[db] kvdb init failed (%d)", (int)rc);
        return -1;
    }

    /* 3. BF extension — "bf_data" is the FAL data partition */
    rc = fdb_bf_init(&s_bf, &s_kvdb, "bf_data", NULL);
    printf("[db] bf init rc=%d", (int)rc);
    if (rc != FDB_NO_ERR)
    {
        printf("[db] bf init failed (%d)", (int)rc);
        return -1;
    }

    /* 4. Enumerate all big files present at boot (equivalent to fdb_get_file_addr loop) */
    printf("[db] big file directory:");

    // construct resouce list
    uint32_t file_num = 32;
    void **file_array = NULL;

    file_array = malloc(sizeof(void *) * file_num * 2); // addr + size
    memset((void *)file_array, 0, file_num * 4 * 2);
    fdb_bf_foreach(&s_bf, bf_boot_enum_cb, (void *)file_array);
    file_num = (uint32_t)file_array[0];

    extern uint8_t mainface_list_init(void **data_list, uint32_t n);
    mainface_list_init(&file_array[1], file_num);

    free(file_array);

    /* Tear badges go last: the built-ins and received wallpapers are already in
     * place, and mainface_list_init() only understands wallpapers. */
    extern uint8_t mainface_list_add_tear(void *preview_addr);
    for (uint32_t i = 0; i < s_tear_num; i++)
    {
        (void)mainface_list_add_tear(s_tear_info[2 * i]);
    }
    printf("[db] restored %u tear badge(s)", (unsigned)s_tear_num);

    /* Restore the persisted UI state (brightness, view toggles, screen-off
     * time, current mainface index...).  Runs after the list is built so the
     * loader can clamp mainface_idx against mainface_num. */
    extern void mainface_user_settings_load(void);
    mainface_user_settings_load();

    return 0;
}



