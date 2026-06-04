/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * NOR Flash port for file_db. See file_db_port_nor_flash.c for the
 * design notes.
 */
#ifndef _FILE_DB_PORT_NOR_FLASH_H_
#define _FILE_DB_PORT_NOR_FLASH_H_

#include <stdint.h>
#include "file_db_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*fdb_nor_hal_read_t)(uint32_t abs_addr, void *buf, uint32_t len);
typedef int (*fdb_nor_hal_write_t)(uint32_t abs_addr, const void *buf, uint32_t len);
typedef int (*fdb_nor_hal_erase_t)(uint32_t abs_addr_sector_aligned);

typedef struct
{
    fdb_nor_hal_read_t   read;
    fdb_nor_hal_write_t  program;
    fdb_nor_hal_erase_t  erase_sector;
    uint32_t             base_addr;
    uint32_t             region_size;
    uint32_t             sector_size;
    uint32_t             page_size;
    uint32_t             dir_bytes;            /* == file_db data_offset */
    uint8_t             *dir_cache;            /* RAM buffer            */
    uint32_t             dir_cache_capacity;
} fdb_nor_cfg_t;

int                    fdb_port_nor_setup(const fdb_nor_cfg_t *cfg);
int                    fdb_port_nor_flush(void);
const fdb_port_ops_t  *fdb_port_nor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* _FILE_DB_PORT_NOR_FLASH_H_ */
