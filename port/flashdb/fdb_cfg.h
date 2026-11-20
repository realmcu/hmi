/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB feature configuration -- eBadge platform
 * This file is maintained by the app project; the FlashDB repo does not include it.
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* Enable the key-value database (KVDB) */
#define FDB_USING_KVDB

/* Enable the BigFile large-file extension (BF) -- requires FDB_USING_FAL_MODE
 * BF directory entries live in KVDB; large-file data lives in the separate "bf_data" FAL partition */
#define FDB_USING_BF

/* Enable the time-series database (TSDB); leave commented out if not needed */
/* #define FDB_USING_TSDB */

/* Use FAL storage mode (access Flash through the FAL layer) */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/* Flash write granularity, in bits; use 1 for NOR Flash */
#define FDB_WRITE_GRAN 1
#endif

/* Little-endian (RTL87x2G is ARM Cortex-M, little-endian by default) */
/* #define FDB_BIG_ENDIAN */

/* Log output macro, using Zephyr printk */
#include <zephyr/sys/printk.h>
#define FDB_PRINT(...) printk(__VA_ARGS__)

/* Enable debug output; comment out for a release build */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
