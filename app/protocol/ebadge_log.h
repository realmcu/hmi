/**
 * @file    ebadge_log.h
 * @brief   Thin log wrapper for the eBadge V1.2 protocol stack.
 *
 * Backed by libc printf so lines land wherever the platform's stdout is
 * hooked up (SEGGER RTT / UART / semihosting depending on soc.c config).
 * Prior versions rode APP_PRINT_xxx / TRACE_BINARY -- switched off so the
 * protocol stack does not depend on the trace ring's tag database.
 *
 * All macros append a newline.  Levels get a short tag:
 *   INFO  -> "[eb] "
 *   WARN  -> "[eb][W] "
 *   ERROR -> "[eb][E] "
 *
 * Two macro flavours coexist:
 *   - Variadic:   EBADGE_LOG("fmt", a, b, c)      -- preferred, works with 0..N args
 *   - Numbered:   EBADGE_LOG1/_LOG2/_LOG3/_LOG4   -- kept for existing call sites
 * The numbered forms are aliases onto the variadic one; both compile to the
 * same printf.
 *
 * Hex dump helper prints up to 32 bytes to keep the output ring readable
 * during high-rate DATA bursts; longer inputs get a "..." marker.
 */
#ifndef _EBADGE_LOG_H_
#define _EBADGE_LOG_H_

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Core printf-backed macros
 *
 *  GCC ##__VA_ARGS__ swallows the leading comma when the variadic list is
 *  empty, so both EBADGE_LOG("hi") and EBADGE_LOG("x=%d", 1) compile.  The
 *  RTK toolchain is GCC; if you ever port this to a strict-C11 compiler,
 *  switch to __VA_OPT__(,) __VA_ARGS__.
 *----------------------------------------------------------------------------*/
#define EBADGE_PRINTF_(tag, fmt, ...)                                          \
    do { (void)printf("[eb]" tag " " fmt "\n", ##__VA_ARGS__); } while (0)

#define EBADGE_LOG(fmt, ...)      EBADGE_PRINTF_("",    fmt, ##__VA_ARGS__)
#define EBADGE_WARN(fmt, ...)     EBADGE_PRINTF_("[W]", fmt, ##__VA_ARGS__)
#define EBADGE_ERR(fmt, ...)      EBADGE_PRINTF_("[E]", fmt, ##__VA_ARGS__)

/* Numbered aliases -- kept so existing call sites (EBADGE_LOG1 / _LOG2 ...)
 * do not need to be touched.  All route through the variadic form.          */
#define EBADGE_LOG1(fmt, a)             EBADGE_LOG(fmt, a)
#define EBADGE_LOG2(fmt, a, b)          EBADGE_LOG(fmt, a, b)
#define EBADGE_LOG3(fmt, a, b, c)       EBADGE_LOG(fmt, a, b, c)
#define EBADGE_LOG4(fmt, a, b, c, d)    EBADGE_LOG(fmt, a, b, c, d)

#define EBADGE_WARN1(fmt, a)            EBADGE_WARN(fmt, a)
#define EBADGE_WARN2(fmt, a, b)         EBADGE_WARN(fmt, a, b)

#define EBADGE_ERR1(fmt, a)             EBADGE_ERR(fmt, a)
#define EBADGE_ERR2(fmt, a, b)          EBADGE_ERR(fmt, a, b)

/*----------------------------------------------------------------------------*
 *  Hex dump helper -- prefix + up to 32 bytes ("..." if truncated).
 *
 *  Two lines total: one "<tag> len=N" line, one hex line.  Splitting keeps
 *  each printf() call short so lock-free stdout ports don't have to reserve
 *  a giant scratch buffer.
 *----------------------------------------------------------------------------*/
#define EBADGE_LOG_HEX(tag, buf, len)                                          \
    do {                                                                       \
        const uint8_t *_b = (const uint8_t *)(buf);                            \
        uint16_t       _l = (uint16_t)(len);                                   \
        uint16_t       _n = (uint16_t)(_l > 32 ? 32 : _l);                     \
        (void)printf("[eb] %s len=%d data=", (tag), (int)_l);                  \
        for (uint16_t _i = 0; _i < _n; _i++) {                                 \
            (void)printf("%02x ", _b[_i]);                                     \
        }                                                                      \
        (void)printf("%s\n", (_l > _n) ? "..." : "");                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_LOG_H_ */
