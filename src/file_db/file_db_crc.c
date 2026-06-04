/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * Software CRC32 (poly 0xEDB88320), table-less so no .rodata cost.
 * Throughput is fine for the small bursts file_db produces (entry CRCs
 * over ~32 bytes, occasional data CRC over a whole file).
 */
#include "file_db_internal.h"

uint32_t fdb_crc32_init(void)
{
    return 0xFFFFFFFFu;
}

uint32_t fdb_crc32_finish(uint32_t state)
{
    return state ^ 0xFFFFFFFFu;
}

uint32_t fdb_crc32(uint32_t state, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len--)
    {
        state ^= *p++;
        for (int i = 0; i < 8; ++i)
        {
            uint32_t mask = -(int32_t)(state & 1u);
            state = (state >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return state;
}
