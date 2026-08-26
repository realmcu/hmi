/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB Feature Configuration — eBadge Platform
 * This file is maintained by the application project; the FlashDB repository itself does not contain this file.
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* Enable Key-Value Database (KVDB) */
#define FDB_USING_KVDB

/* Enable Time Series Database (TSDB) */
#define FDB_USING_TSDB

/*
 * Store TSDB index timestamps as signed 64-bit values. The application-level
 * wall clock remains uint32_t (1970..2106), but the default signed 32-bit
 * fdb_time_t would reinterpret values after 2038-01-19 as negative.
 *
 * This changes the on-flash TSDB sector-header and log-index layouts. Devices
 * upgrading from a build that used 32-bit timestamps must erase/recreate the
 * fdb_tsdb1 (pedo) partition as part of that breaking development upgrade.
 */
#define FDB_USING_TIMESTAMP_64BIT

/* Enable Big File extension (depends on KVDB + FAL_MODE) */
#define FDB_USING_BF

/* Use FAL storage mode (access Flash through FAL layer) */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/* Flash write granularity, unit: bit. For NOR Flash set to 1 */
#define FDB_WRITE_GRAN 1
#endif

/* Little-endian (RTL87x2G is ARM Cortex-M, default little-endian) */
/* #define FDB_BIG_ENDIAN */

/* Log output macro, uses Zephyr printk */
#include <zephyr/sys/printk.h>
#define FDB_PRINT(...) printk(__VA_ARGS__)

/* Enable debug info output, comment out for release builds */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
