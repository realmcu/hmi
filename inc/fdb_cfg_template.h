/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief configuration template file. You need to rename the file to "fbd_cfg.h" and modify the configuration items in it to suit your use.
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* using KVDB feature */
#define FDB_USING_KVDB

#ifdef FDB_USING_KVDB
/* Auto update KV to latest default when current KVDB version number is changed. @see fdb_kvdb.ver_num */
/* #define FDB_KV_AUTO_UPDATE */
#endif

/* using TSDB (Time series database) feature */
#define FDB_USING_TSDB

/* Use fixed-size blobs in TSDB to save flash overhead (8 bytes per entry).
 * Define this to the fixed blob size in bytes when all TSL entries are the same size.
 * Ideal for logging fixed-size sensor data (e.g., float + timestamp).
 * Warning: If defined will be incompatible with variable blob flash store or if fixed blob size is later changed */
/* #define FDB_TSDB_FIXED_BLOB_SIZE 4 */

/* Using FAL storage mode */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/* the flash write granularity, unit: bit
 * only support 1(nor flash)/ 8(stm32f2/f4)/ 32(stm32f1)/ 64(stm32f7)/ 128(stm32h5)/ 256(stm32h7) */
#define FDB_WRITE_GRAN                /* @note you must define it for a value */
#endif

/* Using file storage mode by LIBC file API, like fopen/fread/fwrte/fclose */
/* #define FDB_USING_FILE_LIBC_MODE */

/* Using file storage mode by POSIX file API, like open/read/write/close */
/* #define FDB_USING_FILE_POSIX_MODE */

/* Using the Big File (BF) extension.
 * Requires FDB_USING_FAL_MODE and a NOR flash data partition (write_gran == 1).
 * A user-shared KVDB holds 24-byte directory entries under the "bf/" prefix, while
 * bulk data lives in a dedicated FAL partition managed by a rotating allocator. */
/* #define FDB_USING_BF */

#ifdef FDB_USING_BF
/* KV name prefix reserved for Big File directory entries. Default: "bf/" */
/* #define FDB_BF_KEY_PREFIX           "bf/" */

/* max user file key length including the terminating '\0'. Default: 32 */
/* #define FDB_BF_KEY_MAX              32 */

/* max number of concurrently open/writing file handles. Default: 4 */
/* #define FDB_BF_MAX_OPEN_HANDLES     4 */

/* max number of files tracked by the allocator (occupancy table size). Default: 32 */
/* #define FDB_BF_MAX_ENTRIES          32 */
#endif

/* MCU Endian Configuration, default is Little Endian Order. */
/* #define FDB_BIG_ENDIAN */

/* log print macro. default EF_PRINT macro is printf() */
/* #define FDB_PRINT(...)              my_printf(__VA_ARGS__) */

/* print debug information */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
