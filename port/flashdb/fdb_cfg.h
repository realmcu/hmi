/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB configuration -- eBadge platform
 * This file is maintained by the application project; the FlashDB repository itself does not include this file.
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

 /* Enable Key-Value Database (KVDB) */
#define FDB_USING_KVDB

 /* Enable Time Series Database (TSDB) */
#define FDB_USING_TSDB

 /* Enable Big File extension (depends on KVDB + FAL_MODE) */
#define FDB_USING_BF

 /* Use FAL storage mode (access Flash through FAL layer) */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
 /* Flash write granularity, unit: bit, set to 1 for NOR Flash */
#define FDB_WRITE_GRAN 1
#endif

 /* Little-endian (RTL87x2G is ARM Cortex-M, little-endian by default) */
/* #define FDB_BIG_ENDIAN */

 /* Log output macro, uses Zephyr printk */
#include <zephyr/sys/printk.h>
#define FDB_PRINT(...) printk(__VA_ARGS__)

 /* Enable debug output; comment out for release builds */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
