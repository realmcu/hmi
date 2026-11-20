/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side FlashDB config; must match the firmware's port/flashdb/fdb_cfg.h.
 *
 * These macros define the on-flash byte layout (FDB_WRITE_GRAN in particular
 * sets the status-table width and struct padding).  A mismatch produces an
 * image the device cannot read; pack.py compares the functional macros and
 * refuses to build if they differ.
 */
#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

#define FDB_USING_KVDB
#define FDB_USING_BF
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
#define FDB_WRITE_GRAN 1
#endif

/* Host side only prints; no zephyr printk. */
#include <stdio.h>
#define FDB_PRINT(...) fprintf(stderr, __VA_ARGS__)
/* FDB_DEBUG_ENABLE is not defined: no per-KV debug output when packing. */

#endif /* _FDB_CFG_H_ */
