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
 *  Direction tags
 *
 *  Four data paths cross this firmware and their log lines used to be
 *  indistinguishable -- "port=5004" told you nothing about whether that was a
 *  value the phone sent us, one the 8711 reported, or one we invented.  Tagging
 *  by source and destination is what makes a wrong value traceable to the hop
 *  that produced it.  Use these as the first thing in the format string:
 *
 *    [phone->8773]  inbound BLE command / TLV from the App
 *    [8773->phone]  outbound BLE notify -- what the App will actually act on
 *    [8711->8773]   inbound over SPI: AT replies, slot payloads
 *    [8773->8711]   outbound over SPI: AT commands we stage
 *
 *  Kept as plain string literals rather than macros so they concatenate into
 *  the format string at compile time and cost nothing at runtime.
 *----------------------------------------------------------------------------*/
#define EB_DIR_FROM_PHONE   "[phone->8773] "
#define EB_DIR_TO_PHONE     "[8773->phone] "
#define EB_DIR_FROM_8711    "[8711->8773] "
#define EB_DIR_TO_8711      "[8773->8711] "

/*----------------------------------------------------------------------------*
 *  Multi-line body dump
 *
 *  For AT reply bodies, which are CRLF-separated "KEY=value" lines.  Printing
 *  one with a plain LOG puts embedded newlines inside a tagged line, so the
 *  continuation lines lose the "[eb]" prefix and interleave unreadably with
 *  other threads' output.  This re-prefixes every line.
 *
 *  A function rather than a macro: it needs a loop and a bounded per-line copy,
 *  and every call site would otherwise pay for that inline.  Bounded at 96 B
 *  per line and 24 lines -- a WLSTATE reply with several clients fits, and a
 *  malformed body with no newline at all cannot spin.
 *----------------------------------------------------------------------------*/
void ebadge_log_lines(const char *prefix, const char *body);

/*----------------------------------------------------------------------------*
 *  Canonical hex + ASCII dump  --  hex on the left, printable chars on the right
 *
 *      [eb] [8711->8773] slot len=4096 (first 64 B)
 *      [eb] [8711->8773] slot  0000  41 54 4d 43 01 00 00 00  10 00 00 00 ... |ATMC............|
 *
 *  16 bytes per row, so a byte's column is its offset modulo 16 and a header
 *  field's position is readable without counting.  One printf per row rather
 *  than one per byte: the per-byte form interleaves with other threads' output
 *  and gives a 64-byte dump 64 chances to be cut in half.
 *
 *  A function, not a macro -- it needs a row buffer and two loops, and the
 *  transport thread runs on 2 KiB of stack.
 *
 *  @param max  cap on bytes shown.  The header always prints the TRUE length
 *              next to the shown one, so a truncated dump cannot be misread as
 *              a short packet.  Use EBADGE_HEXDUMP_DEFAULT unless you have a
 *              reason: this is called on the SPI transport thread, which holds
 *              B2W low for the whole call and so back-pressures the 8711.
 *----------------------------------------------------------------------------*/
void ebadge_log_hexdump(const char *prefix, const void *buf, uint32_t len,
                        uint32_t max);

/** Bytes worth seeing from a 4096 B slot: every framing header in this protocol
 *  (ATMC 12 B, JPGS 16 B, EBFS 64 B) fits, with the start of the body after it. */
#define EBADGE_HEXDUMP_DEFAULT  64U

/*----------------------------------------------------------------------------*
 *  Hex dump helper -- prefix + up to 32 bytes ("..." if truncated).
 *
 *  Two lines total: one "<tag> len=N" line, one hex line.  Splitting keeps
 *  each printf() call short so lock-free stdout ports don't have to reserve
 *  a giant scratch buffer.
 *
 *  Kept for the existing call sites that only want a one-line head.  For
 *  anything you actually need to read a header out of, use
 *  ebadge_log_hexdump() instead -- it has the ASCII column.
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
