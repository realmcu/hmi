/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB functional test API
 *
 * Generic API with no OS / RTOS dependency; callable from any platform.
 * The Zephyr Shell commands (fdb test / fdb boot_count / fdb init) are
 * auto-registered by flashdb_test.c under CONFIG_SHELL, no explicit call needed.
 */

#ifndef _FLASHDB_TEST_H_
#define _FLASHDB_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize FAL + KVDB (idempotent, safe to call repeatedly)
 * @return 0 on success, -1 on failure
 */
int  flashdb_test_init(void);

/**
 * @brief Read the power-loss counter, increment it and write it back
 */
void flashdb_test_boot_count(void);

/**
 * @brief Run the full test (boot_count + string KV + struct blob KV)
 */
void flashdb_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* _FLASHDB_TEST_H_ */
