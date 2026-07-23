/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB functional test suite.
 *
 * Layout:
 *   flashdb_test_kv_blob.c    — KVDB blob API (struct / integer)
 *   flashdb_test_kv_string.c  — KVDB string API (NUL-terminated)
 *   flashdb_test_ts_auto.c    — TSDB append (get_time cb) + iter + count
 *   flashdb_test_ts_with_ts.c — TSDB append_with_ts (explicit ts) + monotonic
 *                               violation demo + iter + count
 *   flashdb_test_bf.c         — Big File create + append + commit + XIP read
 *   flashdb_test.c            — Aggregate entry + Zephyr shell registration
 *
 * All test entry points are OS-agnostic and can be called from any context
 * after flashdb_registry_init() succeeds.
 */

#ifndef _FLASHDB_TEST_H_
#define _FLASHDB_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Compat shims kept from the old flashdb_test.c so anyone who still calls
 * these keeps working. Prefer the dedicated *_run entries below for new code. */
int  flashdb_test_init(void);        /* forwards to flashdb_registry_init */
void flashdb_test_boot_count(void);  /* alias for the boot_count portion of kv_blob */
void flashdb_test_run(void);         /* runs all subtests in sequence */

/* Per-subsystem test entries. Each is idempotent and prints its own banner. */
void flashdb_test_kv_blob_run(void);
void flashdb_test_kv_string_run(void);
void flashdb_test_ts_auto_run(void);
void flashdb_test_ts_with_ts_run(void);
void flashdb_test_ts_run(void);      /* runs auto + with_ts back-to-back */
void flashdb_test_bf_run(void);

/* Boot-count helper is exposed because it changes flash state at each boot
 * (used as the classic "did we init OK across reboot?" sanity check). */
void flashdb_test_kv_blob_boot_count(void);

#ifdef __cplusplus
}
#endif

#endif /* _FLASHDB_TEST_H_ */
