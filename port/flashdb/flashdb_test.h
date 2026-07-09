/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB functional test interface
 *
 * Generic interface, does not depend on any OS / RTOS, can be called from any platform.
 * Zephyr Shell commands (fdb test / fdb boot_count / fdb init) are
 * automatically registered by flashdb_test.c under CONFIG_SHELL
 * conditional compilation; no explicit call required.
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
 * @brief Read boot count, increment, and write back
 */
void flashdb_test_boot_count(void);

/**
 * @brief Run full test (boot_count + string KV + struct blob KV)
 */
void flashdb_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* _FLASHDB_TEST_H_ */
