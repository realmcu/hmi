/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB KVDB 功能测试接口
 *
 * 通用接口，不依赖任何 OS / RTOS，任意平台均可调用。
 * Zephyr Shell 命令（fdb test / fdb boot_count / fdb init）由
 * flashdb_test.c 在 CONFIG_SHELL 条件编译下自动注册，无需显式调用。
 */

#ifndef _FLASHDB_TEST_H_
#define _FLASHDB_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 FAL + KVDB（幂等，重复调用安全）
 * @return 0 成功，-1 失败
 */
int  flashdb_test_init(void);

/**
 * @brief 读取掉电计数并递增写回
 */
void flashdb_test_boot_count(void);

/**
 * @brief 运行完整测试（boot_count + string KV + struct blob KV）
 */
void flashdb_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* _FLASHDB_TEST_H_ */
