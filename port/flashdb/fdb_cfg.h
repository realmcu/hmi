/*
 * Copyright (c) 2024, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * FlashDB 功能配置 —— eBadge 平台
 * 此文件由应用工程维护，FlashDB 仓库本身不包含此文件。
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* 启用键值数据库 (KVDB) */
#define FDB_USING_KVDB

/* 启用时序数据库 (TSDB)，暂不需要可保持注释 */
/* #define FDB_USING_TSDB */

/* 使用 FAL 存储模式（通过 FAL 层访问 Flash） */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/* Flash 写粒度，单位：bit，NOR Flash 填 1 */
#define FDB_WRITE_GRAN 1
#endif

/* 小端序（RTL87x2G 为 ARM Cortex-M，默认小端） */
/* #define FDB_BIG_ENDIAN */

/* 日志输出宏，使用 Zephyr printk */
#include <zephyr/sys/printk.h>
#define FDB_PRINT(...) printk(__VA_ARGS__)

/* 开启调试信息输出，正式发布可注释掉 */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
