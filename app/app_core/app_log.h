#ifndef __APP_LOG_H__
#define __APP_LOG_H__

/**
 * @file  app_log.h
 * @brief App-layer logging backed by the RTL87x3G trace interface.
 *
 * Business code keeps using APP_LOGI/W/E/D without depending directly on
 * Zephyr logging. This adapter forwards each call to trace.h's APP_PRINT_*
 * macros so application logs follow the same trace path as the Bluetooth and
 * platform layers. String arguments must be wrapped with TRACE_STRING(), e.g.
 * APP_LOGI("device %s", TRACE_STRING(name)); the trace decoder uses that tag
 * to distinguish a string parameter from an ordinary integer.
 */

#include "trace.h"

/*
 * Preserve the existing per-file registration syntax. Trace output is tagged
 * with MODULE_APP by trace.h, so this only records a local, unused tag.
 */
#define APP_LOG_MODULE_REGISTER(name) \
    static const char *const _app_log_tag_##name __attribute__((unused)) = #name

#define APP_LOG_CAT_(a, b) a##b
#define APP_LOG_CAT(a, b) APP_LOG_CAT_(a, b)
#define APP_LOG_ARG_COUNT(...) \
    APP_LOG_ARG_COUNT_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define APP_LOG_ARG_COUNT_(_0, _1, _2, _3, _4, _5, _6, _7, _8, n, ...) n
#define APP_LOG_DISPATCH(prefix, count, ...) \
    APP_LOG_DISPATCH_(prefix, count, __VA_ARGS__)
#define APP_LOG_DISPATCH_(prefix, count, ...) \
    APP_LOG_CAT(prefix, count)(__VA_ARGS__)

#define APP_LOGI(...) \
    APP_LOG_DISPATCH(APP_PRINT_INFO, APP_LOG_ARG_COUNT(__VA_ARGS__), __VA_ARGS__)
#define APP_LOGW(...) \
    APP_LOG_DISPATCH(APP_PRINT_WARN, APP_LOG_ARG_COUNT(__VA_ARGS__), __VA_ARGS__)
#define APP_LOGE(...) \
    APP_LOG_DISPATCH(APP_PRINT_ERROR, APP_LOG_ARG_COUNT(__VA_ARGS__), __VA_ARGS__)
#define APP_LOGD(...) \
    APP_LOG_DISPATCH(APP_PRINT_TRACE, APP_LOG_ARG_COUNT(__VA_ARGS__), __VA_ARGS__)

#endif /* __APP_LOG_H__ */
