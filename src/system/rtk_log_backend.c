/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*
 * Log backend that forwards Zephyr log messages to the Realtek log channel --
 * the same one DBG_DIRECT() uses and the Realtek debug analyzer displays.
 *
 * Why: every Realtek trace (DBG_DIRECT, APP_PRINT_*) goes to the RTK log
 * channel, while the Zephyr log would end up on the uart2 console. Having the
 * two halves of the log in different places makes a crash hard to read. With
 * this backend everything the log subsystem emits -- including the register
 * dump from z_arm_fault() and the coredump -- lands next to the Realtek traces
 * in the analyzer.
 *
 * Destination is selected in prj.conf (see the "Logging destination" block):
 *   - A, analyzer only:     CONFIG_RTK_LOG_BACKEND=y, CONFIG_LOG_BACKEND_UART=n,
 *                           CONFIG_CONSOLE=n
 *   - B, console only:      CONFIG_RTK_LOG_BACKEND=n, CONFIG_LOG_BACKEND_UART=y,
 *                           CONFIG_CONSOLE=y -- this file compiles to nothing
 *                           and printf() falls back to the newlib/console path
 *   - C, both:              CONFIG_RTK_LOG_BACKEND=y + CONFIG_LOG_BACKEND_UART=y
 *                           + CONFIG_CONSOLE=y
 * In A and C printf() follows the log destination; it mirrors to the console
 * only when the console is a log destination too, so the two never disagree.
 *
 * The channel is a ROM printf-style API, so the message is formatted by the
 * log output formatter into a RAM line buffer first and handed over one line
 * at a time. It polls the UART, so it also works from the fault handler.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include <stdarg.h>
#include <stdio.h>

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>

#include "trace.h"

#if defined(CONFIG_LOG) && defined(CONFIG_RTK_LOG_BACKEND)

/* Scratch buffer for the log output formatter. */
static uint8_t fmt_buf[64];

/* One assembled line; longer lines are split. */
#define RTK_LINE_SIZE 200
static char rtk_line[RTK_LINE_SIZE];
static size_t rtk_line_len;

static void rtk_line_flush(void)
{
    if (rtk_line_len == 0)
    {
        return;
    }

    rtk_line[rtk_line_len] = '\0';
    DBG_DIRECT("%s", rtk_line);
    rtk_line_len = 0;
}

static int rtk_char_out(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);

    for (size_t i = 0; i < length; i++)
    {
        char c = (char)data[i];

        /* CRLF is handled by the channel, not by us. */
        if (c == '\r')
        {
            continue;
        }
        if (c == '\n')
        {
            rtk_line_flush();
            continue;
        }
        if (rtk_line_len + 1 >= sizeof(rtk_line))
        {
            rtk_line_flush();
        }
        rtk_line[rtk_line_len++] = c;
    }

    return (int)length;
}

LOG_OUTPUT_DEFINE(rtk_log_output, rtk_char_out, fmt_buf, sizeof(fmt_buf));

static void rtk_log_process(const struct log_backend *const backend,
                            union log_msg_generic *msg)
{
    /* No ANSI colors or Zephyr timestamps: the analyzer adds its own. */
    uint32_t flags = log_backend_std_get_flags() &
                     ~(LOG_OUTPUT_FLAG_COLORS | LOG_OUTPUT_FLAG_TIMESTAMP);

    ARG_UNUSED(backend);

    log_output_msg_process(&rtk_log_output, &msg->log, flags);

    /* Not every message ends with a newline. */
    rtk_line_flush();
}

static void rtk_log_dropped(const struct log_backend *const backend, uint32_t cnt)
{
    ARG_UNUSED(backend);

    log_backend_std_dropped(&rtk_log_output, cnt);
}

static void rtk_log_panic(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);

    log_backend_std_panic(&rtk_log_output);
    rtk_line_flush();
}

static const struct log_backend_api rtk_log_backend_api =
{
    .process = rtk_log_process,
    .dropped = rtk_log_dropped,
    .panic = rtk_log_panic,
};

LOG_BACKEND_DEFINE(rtk_log_backend, rtk_log_backend_api, true);

/*
 * printf() bridge.
 *
 * LOG_* reaches both the Zephyr console (LOG_BACKEND_UART) and the Realtek log
 * channel (this backend), so printf() should behave the same way instead of
 * silently disappearing or landing on one side only.
 *
 * This also replaces the SDK hook in soc.c, which is compiled out together with
 * CONFIG_LOG_RTK, and which is broken anyway: it calls log_direct(fmt) without
 * forwarding the varargs, so any format specifier would print garbage.
 */
#define RTK_PRINTF_BUF_SIZE 200

/*
 * Mirror printf() to the Zephyr console as well, but only when the console is
 * also a log destination -- otherwise it would be the one thing still writing
 * to uart2 while everything else went to the analyzer.
 */
#if defined(CONFIG_LOG_BACKEND_UART) && DT_HAS_CHOSEN(zephyr_console)
#define RTK_PRINTF_MIRROR_CONSOLE 1
static const struct device *const rtk_console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif

int printf(const char *fmt, ...)
{
    char buf[RTK_PRINTF_BUF_SIZE];
    va_list ap;
    int len;
    int n;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0)
    {
        return len;
    }

    /* vsnprintf returns the untruncated length; buf is always terminated. */
    n = MIN(len, (int)sizeof(buf) - 1);

    DBG_DIRECT("%s", buf);

#ifdef RTK_PRINTF_MIRROR_CONSOLE
    for (int i = 0; i < n; i++)
    {
        uart_poll_out(rtk_console, buf[i]);
    }
#endif

    return len;
}

#endif /* CONFIG_LOG && CONFIG_RTK_LOG_BACKEND */
