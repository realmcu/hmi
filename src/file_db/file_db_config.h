/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */
#ifndef _FILE_DB_CONFIG_H_
#define _FILE_DB_CONFIG_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of directory slots (== max files, including DELETED tombstones). */
#ifndef FDB_MAX_ENTRIES
#define FDB_MAX_ENTRIES             32
#endif

/* Data-region allocation alignment, in bytes. Must be a power of two. */
#ifndef FDB_DATA_ALIGN
#define FDB_DATA_ALIGN              4096u
#endif

/* Maximum simultaneously open file handles (static pool, no malloc). */
#ifndef FDB_MAX_OPEN_HANDLES
#define FDB_MAX_OPEN_HANDLES        4
#endif

/* Log control. */
#ifndef FDB_LOG_ENABLE
#define FDB_LOG_ENABLE              1
#endif

#if FDB_LOG_ENABLE
#include "trace.h"
// #define FDB_LOG(fmt, ...)           printf("[FDB] " fmt "\r\n", ##__VA_ARGS__)
#define FDB_LOG(fmt, ...)           DBG_DIRECT("[FDB] " fmt "\r\n", ##__VA_ARGS__)
#else
#define FDB_LOG(fmt, ...)           do {} while (0)
#endif

/* Hex bytes per line in fdb_dump_file(). */
#ifndef FDB_LOG_HEX_PER_LINE
#define FDB_LOG_HEX_PER_LINE        16u
#endif

#ifdef __cplusplus
}
#endif

#endif /* _FILE_DB_CONFIG_H_ */
