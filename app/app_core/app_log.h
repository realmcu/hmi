#ifndef __APP_LOG_H__
#define __APP_LOG_H__

/**
 * @file  app_log.h
 * @brief App-layer logging — the ONLY logging entry point for app code.
 *
 * App-layer files must NEVER include a platform log header directly
 * (@c zephyr/logging/log.h etc.) and must NEVER call platform log
 * primitives (@c LOG_INF , @c LOG_MODULE_REGISTER , @c DBG_DIRECT ). All
 * logging goes through the macros below. This way the whole app layer
 * stays platform-agnostic: swapping the host platform touches only this
 * file, and every business module keeps working unchanged.
 *
 * Usage
 * -----
 *   #include "app_log.h"
 *   APP_LOG_MODULE_REGISTER(app_ble);   // once per .c file, near the top
 *
 *   APP_LOGI("subscribed to %s", "EVT_TIME_TICK_MIN");
 *   APP_LOGE("failed rc=%d", rc);
 *
 * The tag passed to @c APP_LOG_MODULE_REGISTER is a bare identifier
 * (unquoted), matching the convention of platform log frameworks. On
 * Zephyr it becomes the module name shown in every log line
 * (e.g. @c "<inf> app_ble: ..." ). On other platforms it can be
 * embedded literally into the printf output.
 *
 * Levels are the usual four: I (info), W (warn), E (error), D (debug).
 * Zero-arg calls are supported:
 *   APP_LOGI("hello");
 */

#if defined(__ZEPHYR__)

#include <zephyr/logging/log.h>

/**
 * @def APP_LOG_MODULE_REGISTER
 * @brief Register a per-file log tag. Call once near the top of each .c.
 */
#define APP_LOG_MODULE_REGISTER(name)   LOG_MODULE_REGISTER(name, LOG_LEVEL_INF)

#define APP_LOGI(fmt, ...)              LOG_INF(fmt, ##__VA_ARGS__)
#define APP_LOGW(fmt, ...)              LOG_WRN(fmt, ##__VA_ARGS__)
#define APP_LOGE(fmt, ...)              LOG_ERR(fmt, ##__VA_ARGS__)
#define APP_LOGD(fmt, ...)              LOG_DBG(fmt, ##__VA_ARGS__)

#else /* fallback for non-Zephyr platforms — swap for the target log API */

#include <stdio.h>

/* Store the file's tag in a file-static so the level macros can prefix it. */
#define APP_LOG_MODULE_REGISTER(name)   static const char *_app_log_tag = #name

#define APP_LOGI(fmt, ...)              printf("[I] %s: " fmt "\n", _app_log_tag, ##__VA_ARGS__)
#define APP_LOGW(fmt, ...)              printf("[W] %s: " fmt "\n", _app_log_tag, ##__VA_ARGS__)
#define APP_LOGE(fmt, ...)              printf("[E] %s: " fmt "\n", _app_log_tag, ##__VA_ARGS__)
#define APP_LOGD(fmt, ...)              printf("[D] %s: " fmt "\n", _app_log_tag, ##__VA_ARGS__)

#endif

#endif /* __APP_LOG_H__ */
