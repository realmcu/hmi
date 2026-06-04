/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * RAM-backed port. The buffer is supplied by the caller via
 * fdb_port_ram_setup(). All accesses are plain memcpy / memset, so this
 * port is the reference implementation and is also useful for unit tests.
 */
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "file_db_port.h"

static uint8_t  *s_buf;
static uint32_t  s_size;

int fdb_port_ram_setup(void *buffer, uint32_t size)
{
    if (!buffer || size == 0) { return -1; }
    s_buf  = (uint8_t *)buffer;
    s_size = size;
    return 0;
}

static int ram_init(void)
{
    return (s_buf && s_size) ? 0 : -1;
}

static int ram_read(uint32_t off, void *buf, uint32_t len)
{
    if (!s_buf)                  { return -1; }
    if (off + len < off)         { return -1; }
    if (off + len > s_size)      { return -1; }
    memcpy(buf, s_buf + off, len);
    return 0;
}

static int ram_write(uint32_t off, const void *buf, uint32_t len)
{
    if (!s_buf)                  { return -1; }
    if (off + len < off)         { return -1; }
    if (off + len > s_size)      { return -1; }
    memcpy(s_buf + off, buf, len);
    return 0;
}

static int ram_erase(uint32_t off, uint32_t len)
{
    if (!s_buf)                  { return -1; }
    if (off + len < off)         { return -1; }
    if (off + len > s_size)      { return -1; }
    memset(s_buf + off, 0xFF, len);
    return 0;
}

static uint32_t ram_size(void)
{
    return s_size;
}

static uintptr_t ram_base_addr(void)
{
    return (uintptr_t)s_buf;
}

static const fdb_port_ops_t s_ram_ops =
{
    .init      = ram_init,
    .deinit    = NULL,
    .read      = ram_read,
    .write     = ram_write,
    .erase     = ram_erase,
    .size      = ram_size,
    .base_addr = ram_base_addr,
};

const fdb_port_ops_t *fdb_port_ram_get_ops(void)
{
    return &s_ram_ops;
}
