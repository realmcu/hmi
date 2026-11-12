/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _HMI_BT_LOG_H_
#define _HMI_BT_LOG_H_

/*============================================================================*
 *                   Per-packet HMI traces, behind one switch
 *
 *  While a phone streams over the private service, every GATT write, every
 *  notify and every L1 send used to print a line -- and hmi_ble_ctrl_receive
 *  dumped the whole payload as hex. That is thousands of lines per second on a
 *  shared UART, which both floods the log and costs real CPU time in the BT
 *  task. Those traces belong to service bring-up, not to daily runs, so they
 *  are gated here:
 *
 *      HMI_BT_LOG_ENABLE 0    every HMI_LOG_* below compiles to nothing
 *                             (no format string in flash, no argument
 *                             evaluation, no UART traffic) -- default
 *      HMI_BT_LOG_ENABLE 1    they behave exactly like APP_PRINT_INFO*
 *
 *  Turn them on globally with a build flag (-DHMI_BT_LOG_ENABLE=1), or for a
 *  single file by defining it above the include:
 *
 *      #define HMI_BT_LOG_ENABLE 1     // this translation unit only
 *      #include "hmi_bt_log.h"
 *
 *  Pick the macro that matches the argument count of the call being converted,
 *  so the output keeps its original shape:
 *
 *      APP_PRINT_INFO3("x %d y %d z %d", a, b, c)
 *      HMI_LOG_INFO3 ("x %d y %d z %d", a, b, c)
 *
 *  WARN and ERROR traces are deliberately not wrapped: they are rare and are
 *  the ones worth keeping when a switch is flipped.
 *============================================================================*/

#ifndef HMI_BT_LOG_ENABLE
#define HMI_BT_LOG_ENABLE   0
#endif

#if HMI_BT_LOG_ENABLE

#define HMI_LOG_INFO0(fmt)                      APP_PRINT_INFO0(fmt)
#define HMI_LOG_INFO1(fmt, a)                   APP_PRINT_INFO1(fmt, a)
#define HMI_LOG_INFO2(fmt, a, b)                APP_PRINT_INFO2(fmt, a, b)
#define HMI_LOG_INFO3(fmt, a, b, c)             APP_PRINT_INFO3(fmt, a, b, c)
#define HMI_LOG_INFO4(fmt, a, b, c, d)          APP_PRINT_INFO4(fmt, a, b, c, d)
#define HMI_LOG_INFO5(fmt, a, b, c, d, e)       APP_PRINT_INFO5(fmt, a, b, c, d, e)

#else

/* ((void)0) rather than do {} while (0): it is a single expression statement,
 * so it stays safe under an unbraced if/else at the call site. */
#define HMI_LOG_INFO0(fmt)                      ((void)0)
#define HMI_LOG_INFO1(fmt, a)                   ((void)0)
#define HMI_LOG_INFO2(fmt, a, b)                ((void)0)
#define HMI_LOG_INFO3(fmt, a, b, c)             ((void)0)
#define HMI_LOG_INFO4(fmt, a, b, c, d)          ((void)0)
#define HMI_LOG_INFO5(fmt, a, b, c, d, e)       ((void)0)

#endif /* HMI_BT_LOG_ENABLE */

/*============================================================================*
 *                Navigate-file transfer (cmd 0x10) traces
 *
 *  The file transfer is a small protocol state machine: BEGIN / DATA / END /
 *  ABORT, each with a few rejection paths. Following it line by line while a
 *  transfer misbehaves is exactly what these traces are for; the rest of the
 *  time they are noise in the middle of the streaming logs.
 *
 *      HMI_NAVI_FILE_LOG_ENABLE 0    off (default): every NAVI_FILE_LOG_* below
 *                                    compiles away, format strings included
 *      HMI_NAVI_FILE_LOG_ENABLE 1    on, same severities as the calls they wrap
 *
 *  Independent from HMI_BT_LOG_ENABLE: turning one on does not turn on the
 *  other. Same per-file override trick applies (define it above the include).
 *
 *  ERROR level stays un-gated on purpose -- "transport not bound by the GUI" is
 *  a wiring bug, not transfer chatter, and must survive every switch.
 *============================================================================*/

#ifndef HMI_NAVI_FILE_LOG_ENABLE
#define HMI_NAVI_FILE_LOG_ENABLE   0
#endif

#ifndef HMI_NAVI_FILE_LOG_VERBOSE
/* Per-frame progress lines (BEGIN, frame committed). Off by default: they fire
 * on every frame, and on a shared UART that alone shifts the timing enough to
 * hide the failures the switch above exists to catch. */
#define HMI_NAVI_FILE_LOG_VERBOSE  0
#endif

#if HMI_NAVI_FILE_LOG_ENABLE

#define NAVI_FILE_LOG_INFO1(fmt, a)                 APP_PRINT_INFO1(fmt, a)
#define NAVI_FILE_LOG_WARN0(fmt)                    APP_PRINT_WARN0(fmt)
#define NAVI_FILE_LOG_WARN1(fmt, a)                 APP_PRINT_WARN1(fmt, a)
#define NAVI_FILE_LOG_WARN2(fmt, a, b)              APP_PRINT_WARN2(fmt, a, b)
#define NAVI_FILE_LOG_WARN3(fmt, a, b, c)           APP_PRINT_WARN3(fmt, a, b, c)
#define NAVI_FILE_LOG_WARN5(fmt, a, b, c, d, e)     APP_PRINT_WARN5(fmt, a, b, c, d, e)
#define NAVI_FILE_LOG_TRACE2(fmt, a, b)             APP_PRINT_TRACE2(fmt, a, b)
#define NAVI_FILE_LOG_TRACE3(fmt, a, b, c)          APP_PRINT_TRACE3(fmt, a, b, c)

#else

#define NAVI_FILE_LOG_INFO1(fmt, a)                 ((void)0)
#define NAVI_FILE_LOG_WARN0(fmt)                    ((void)0)
#define NAVI_FILE_LOG_WARN1(fmt, a)                 ((void)0)
#define NAVI_FILE_LOG_WARN2(fmt, a, b)              ((void)0)
#define NAVI_FILE_LOG_WARN3(fmt, a, b, c)           ((void)0)
#define NAVI_FILE_LOG_WARN5(fmt, a, b, c, d, e)     ((void)0)
#define NAVI_FILE_LOG_TRACE2(fmt, a, b)             ((void)0)
#define NAVI_FILE_LOG_TRACE3(fmt, a, b, c)          ((void)0)

#endif /* HMI_NAVI_FILE_LOG_ENABLE */

#if (HMI_NAVI_FILE_LOG_ENABLE && HMI_NAVI_FILE_LOG_VERBOSE)
#define NAVI_FILE_LOG_DEBUG2(fmt, a, b)             APP_PRINT_TRACE2(fmt, a, b)
#define NAVI_FILE_LOG_DEBUG3(fmt, a, b, c)          APP_PRINT_TRACE3(fmt, a, b, c)
#else
#define NAVI_FILE_LOG_DEBUG2(fmt, a, b)             ((void)0)
#define NAVI_FILE_LOG_DEBUG3(fmt, a, b, c)          ((void)0)
#endif

#endif /* _HMI_BT_LOG_H_ */
