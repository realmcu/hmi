/**
 * @file    ebadge_log.c
 * @brief   The one log helper that cannot be a macro.
 *
 * ebadge_log.h is otherwise header-only and should stay that way -- the macros
 * compile straight to printf and cost nothing.  This lives here because dumping
 * a multi-line body needs a loop and a bounded scratch buffer, and inlining that
 * at every call site would put a 96-byte frame on stacks that do not want one
 * (the transport thread runs on 2 KiB).
 */
#include <string.h>
#include "ebadge_log.h"

/* Bounded on both axes so a malformed body cannot spin or overrun.
 *
 * 96 B per line: the longest real line is "PASSWORD=" plus a 63-byte PSK.
 * 24 lines: a WLSTATE reply with a handful of CLIENT= lines.  Both truncate
 * visibly rather than silently -- a body that hits either limit says so, since
 * a dump that quietly stopped early is worse than no dump during bring-up. */
#define LOG_LINE_MAX    96U
#define LOG_LINES_MAX   24U

void ebadge_log_lines(const char *prefix, const char *body)
{
    if (prefix == NULL || body == NULL)
    {
        return;
    }
    if (body[0] == '\0')
    {
        /* Say it explicitly.  An empty body printed as nothing at all is
         * indistinguishable from the dump not having run. */
        (void)printf("[eb] %s<empty>\n", prefix);
        return;
    }

    const char *p     = body;
    unsigned    lines = 0U;

    while (*p != '\0')
    {
        /* Skip the separators first, so a CRLF does not emit a blank line. */
        while (*p == '\r' || *p == '\n')
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        if (lines >= LOG_LINES_MAX)
        {
            (void)printf("[eb] %s... (truncated at %u lines)\n", prefix,
                         (unsigned)LOG_LINES_MAX);
            return;
        }

        char   line[LOG_LINE_MAX];
        size_t n = 0U;

        while (p[n] != '\0' && p[n] != '\r' && p[n] != '\n' &&
               n < (LOG_LINE_MAX - 1U))
        {
            line[n] = p[n];
            n++;
        }
        line[n] = '\0';

        (void)printf("[eb] %s%s\n", prefix, line);
        lines++;

        /* Advance past what was copied.  Note this is n, not the whole line: a
         * line longer than the buffer resumes rather than being skipped, so an
         * over-long PASSWORD= still shows its tail on the next line instead of
         * vanishing. */
        p += n;
    }
}

/*----------------------------------------------------------------------------*
 *  Canonical hex + ASCII dump
 *
 *  16 bytes per row so a row fits a narrow terminal, and so the column a byte
 *  lands in is its offset modulo 16 -- which is what makes a header field's
 *  position readable at a glance without counting.
 *
 *  One printf per row, not one per byte: the per-byte form interleaves with
 *  other threads' output and turns a dump into one chance per byte to be cut in
 *  half.  Building the row in a local buffer first makes each row atomic as far
 *  as any reasonable stdout port is concerned.
 *----------------------------------------------------------------------------*/
#define HEXD_COLS       16U

/* hex field: 16 * 3 chars, plus the gap between the 8-byte halves, plus the
 * ASCII field's 16 chars and its delimiters.  Sized generously and asserted by
 * construction below -- every write is bounded by HEXD_COLS. */
#define HEXD_ROW_MAX    80U

void ebadge_log_hexdump(const char *prefix, const void *buf, uint32_t len,
                        uint32_t max)
{
    const uint8_t *b = (const uint8_t *)buf;

    if (prefix == NULL)
    {
        prefix = "";
    }
    if (b == NULL)
    {
        (void)printf("[eb] %s <null>\n", prefix);
        return;
    }

    uint32_t shown = (len > max) ? max : len;

    /* Header carries the true length next to the shown length, so a truncated
     * dump can never be mistaken for a short packet. */
    if (shown < len)
    {
        (void)printf("[eb] %s len=%u (first %u B)\n", prefix, (unsigned)len,
                     (unsigned)shown);
    }
    else
    {
        (void)printf("[eb] %s len=%u\n", prefix, (unsigned)len);
    }

    for (uint32_t off = 0U; off < shown; off += HEXD_COLS)
    {
        char     row[HEXD_ROW_MAX];
        uint32_t n   = 0U;                 /* write cursor into row            */
        uint32_t cnt = shown - off;

        if (cnt > HEXD_COLS)
        {
            cnt = HEXD_COLS;
        }

        static const char hexd[] = "0123456789abcdef";

        for (uint32_t i = 0U; i < HEXD_COLS; i++)
        {
            if (i == HEXD_COLS / 2U)
            {
                row[n++] = ' ';            /* split the halves                 */
            }
            if (i < cnt)
            {
                row[n++] = hexd[b[off + i] >> 4];
                row[n++] = hexd[b[off + i] & 0x0FU];
            }
            else
            {
                /* Pad a short final row rather than letting the ASCII column
                 * slide left -- a ragged last line breaks the eye's alignment
                 * on exactly the row that is usually the interesting one. */
                row[n++] = ' ';
                row[n++] = ' ';
            }
            row[n++] = ' ';
        }

        row[n++] = '|';

        for (uint32_t i = 0U; i < cnt; i++)
        {
            uint8_t c = b[off + i];

            /* Printable ASCII only.  Everything else becomes '.', including
             * 0x7f and the high half -- a terminal handed a raw control byte
             * may act on it, and a dump that moves the cursor is worse than a
             * dump that loses one character's identity. */
            row[n++] = (c >= 0x20U && c < 0x7FU) ? (char)c : '.';
        }

        row[n++] = '|';
        row[n]   = '\0';

        (void)printf("[eb] %s  %04x  %s\n", prefix, (unsigned)off, row);
    }
}

