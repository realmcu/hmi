/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * Storage port (HAL) interface for file_db.
 *
 * The port abstracts away the underlying medium (RAM, NOR flash, EEPROM, ...).
 * All offsets are relative to the beginning of the file_db region.
 */
#ifndef _FILE_DB_PORT_H_
#define _FILE_DB_PORT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    /* Optional one-shot init / deinit. May be NULL. Return 0 on success. */
    int (*init)(void);
    int (*deinit)(void);

    /* Mandatory. Read / write `len` bytes at `off`. Return 0 on success. */
    int (*read)(uint32_t off, void *buf, uint32_t len);
    int (*write)(uint32_t off, const void *buf, uint32_t len);

    /* Optional. Erase a region (flash-like media). May be NULL on RAM ports. */
    int (*erase)(uint32_t off, uint32_t len);

    /* Mandatory. Total addressable size of the region in bytes. */
    uint32_t (*size)(void);

    /* Optional. Absolute base address of the region (for callers that
     * need a CPU-addressable pointer instead of an in-region offset).
     * May be NULL when the medium has no meaningful absolute address. */
    uintptr_t (*base_addr)(void);
} fdb_port_ops_t;

/* Default RAM-backed port. The buffer must remain valid for the module lifetime. */
const fdb_port_ops_t *fdb_port_ram_get_ops(void);
int                   fdb_port_ram_setup(void *buffer, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* _FILE_DB_PORT_H_ */
