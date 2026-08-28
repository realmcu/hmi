/**
 * @file    at_parser.c
 * @brief   The state machine described in at_parser.h.  No command semantics.
 *
 * Four states, and the whole design is in the transition out of ST_LINE:
 *
 *   ST_LINE      accumulating text.  Two ways out: a CRLF ends a line, or the
 *                accumulated text turns out to be a [$][SKT][DATA] head, which
 *                ends at ':' instead and hands over to ST_DATA.
 *   ST_DATA      copying exactly <len> bytes without inspecting them
 *   ST_EOL_CR    the payload is done; expect the event's trailing CR
 *   ST_EOL_LF    ... then its LF, and we are back to text
 *
 * The two EOL states are not pedantry.  They are the only place a length
 * mis-parse becomes visible: if <len> was wrong, the byte after the payload is
 * not CR, and counting that as a desync is what turns "the pictures are subtly
 * corrupt" into a number someone can read.  On a desync the offending byte is
 * REPROCESSED as text rather than swallowed, so the parser resynchronises on the
 * next real CRLF instead of eating the line after it too.
 */
#if defined(CONFIG_SPI_AT_MASTER)

#include <string.h>
#include <stdlib.h>

#include "at_parser.h"
#include "../protocol/ebadge_log.h"

/* States.  Kept as a plain uint8_t in the struct so a host test can memset it. */
enum
{
    ST_LINE = 0,
    ST_DATA,
    ST_EOL_CR,
    ST_EOL_LF,
};

/* The one prefix that changes how the byte stream is read.  Everything else the
 * firmware emits -- including the other [$] events -- is plain text. */
#define DATA_PREFIX     "[$][SKT][DATA]["
#define DATA_PREFIX_LEN (sizeof(DATA_PREFIX) - 1U)

#define EVENT_PREFIX    "[$]"
#define EVENT_PREFIX_LEN (sizeof(EVENT_PREFIX) - 1U)

/*----------------------------------------------------------------------------*
 *  Head parsing
 *----------------------------------------------------------------------------*/

/* Parse "[$][SKT][DATA][<link>][<len>]:" out of @p s, which is the accumulated
 * line INCLUDING the trailing ':'.
 *
 * Deliberately strict -- every bracket must be where it belongs and both numbers
 * must be plain decimal.  A head we half-understand is worse than one we reject:
 * rejecting keeps the bytes in the text stream where a human can see them, while
 * a wrong <len> silently misaligns everything that follows.
 *
 * @return true and fills the outputs, or false and the caller counts a bad head.
 */
static bool parse_data_head(const char *s, uint16_t len,
                            uint8_t *out_link, uint32_t *out_total)
{
    if (len <= DATA_PREFIX_LEN)
    {
        return false;
    }
    if (memcmp(s, DATA_PREFIX, DATA_PREFIX_LEN) != 0)
    {
        return false;
    }

    const char *p = s + DATA_PREFIX_LEN;
    const char *end = s + len;

    /* <link_id>] */
    uint32_t link = 0;
    uint16_t digits = 0;

    while (p < end && *p >= '0' && *p <= '9')
    {
        link = (link * 10U) + (uint32_t)(*p - '0');
        p++;
        digits++;
        if (digits > 3U)
        {
            return false;           /* link_id is [0,19]; 4 digits is garbage  */
        }
    }
    if (digits == 0U || p >= end || *p != ']')
    {
        return false;
    }
    p++;

    /* [<len>]: */
    if (p >= end || *p != '[')
    {
        return false;
    }
    p++;

    uint32_t total = 0;

    digits = 0;
    while (p < end && *p >= '0' && *p <= '9')
    {
        total = (total * 10U) + (uint32_t)(*p - '0');
        p++;
        digits++;
        if (digits > 5U)
        {
            return false;           /* > 99999 cannot be a slot payload        */
        }
    }
    if (digits == 0U || p >= end || *p != ']')
    {
        return false;
    }
    p++;

    if (p != end - 1 || *p != ':')
    {
        return false;               /* the ':' must be the last byte we have   */
    }

    if (link > 19U || total > AT_PARSER_DATA_MAX)
    {
        return false;
    }

    *out_link  = (uint8_t)link;
    *out_total = total;
    return true;
}

/*----------------------------------------------------------------------------*
 *  Line emission
 *----------------------------------------------------------------------------*/

static void emit_line(at_parser_t *p)
{
    if (p->line_ovf)
    {
        p->stats.line_ovf++;
        EBADGE_WARN(EB_DIR_FROM_8711 "at line > %u B, truncated",
                    (unsigned)(AT_PARSER_LINE_MAX - 1U));
    }

    p->line[p->line_len] = '\0';
    p->stats.lines++;

    if (p->cbs.on_line != NULL)
    {
        p->cbs.on_line(p->line, p->line_len, p->cbs.user);
    }

    p->line_len = 0;
    p->line_ovf = false;
}

/*----------------------------------------------------------------------------*
 *  Public
 *----------------------------------------------------------------------------*/

void at_parser_init(at_parser_t *p, const at_parser_cbs_t *cbs)
{
    if (p == NULL)
    {
        return;
    }

    memset(p, 0, sizeof(*p));
    if (cbs != NULL)
    {
        p->cbs = *cbs;
    }
}

void at_parser_reset(at_parser_t *p)
{
    if (p == NULL)
    {
        return;
    }

    p->state      = ST_LINE;
    p->line_len   = 0;
    p->line_ovf   = false;
    p->data_link  = 0;
    p->data_total = 0;
    p->data_done  = 0;
}

bool at_parser_in_data(const at_parser_t *p)
{
    return (p != NULL) && (p->state != ST_LINE);
}

void at_parser_get_stats(const at_parser_t *p, at_parser_stats_t *out)
{
    if (p != NULL && out != NULL)
    {
        *out = p->stats;
    }
}

bool at_parser_line_is_event(const char *line, uint16_t len)
{
    return (line != NULL) && (len >= EVENT_PREFIX_LEN) &&
           (memcmp(line, EVENT_PREFIX, EVENT_PREFIX_LEN) == 0);
}

void at_parser_feed(at_parser_t *p, const uint8_t *buf, uint16_t len)
{
    if (p == NULL || buf == NULL)
    {
        return;
    }

    uint16_t i = 0;

    while (i < len)
    {
        switch (p->state)
        {
        case ST_DATA:
            {
                /* The hot path, and the reason on_data takes a chunk rather than a
                 * whole payload: what is left of this slot is forwarded as-is,
                 * pointing straight into the slot buffer.  No copy, no 16 KiB of
                 * parser-owned RAM. */
                uint32_t want = p->data_total - p->data_done;
                uint32_t have = (uint32_t)(len - i);
                uint16_t take = (uint16_t)((have < want) ? have : want);

                if (p->cbs.on_data != NULL)
                {
                    p->cbs.on_data(p->data_link, &buf[i], take, p->data_done,
                                   p->data_total,
                                   (p->data_done + take) == p->data_total,
                                   p->cbs.user);
                }

                p->data_done      += take;
                p->stats.data_bytes += take;
                i                 = (uint16_t)(i + take);

                if (p->data_done == p->data_total)
                {
                    p->state = ST_EOL_CR;
                }
                break;
            }

        case ST_EOL_CR:
            if (buf[i] == '\r')
            {
                p->state = ST_EOL_LF;
                i++;
            }
            else
            {
                /* <len> did not describe the payload.  Do not consume the byte:
                 * hand it to the text path so the next genuine CRLF ends a line
                 * and the stream comes back into step. */
                p->stats.desyncs++;
                EBADGE_WARN(EB_DIR_FROM_8711 "at data desync: %02x where CR "
                            "expected after %u B", buf[i],
                            (unsigned)p->data_total);
                p->state = ST_LINE;
            }
            break;

        case ST_EOL_LF:
            if (buf[i] == '\n')
            {
                i++;
            }
            else
            {
                p->stats.desyncs++;
            }
            p->state = ST_LINE;
            break;

        case ST_LINE:
        default:
            {
                char c = (char)buf[i++];

                if (c == '\n')
                {
                    emit_line(p);
                    break;
                }
                if (c == '\r')
                {
                    /* Dropped rather than stored: a line's CR belongs to the framing,
                     * and a lone CR inside text is not something this firmware
                     * emits. */
                    break;
                }

                if (p->line_len < (AT_PARSER_LINE_MAX - 1U))
                {
                    p->line[p->line_len++] = c;
                }
                else
                {
                    p->line_ovf = true;
                    break;      /* nothing below can match once we are truncating  */
                }

                /* A data head ends at ':' instead of at CRLF, so this is where the
                 * stream stops being text.  Only tested when the accumulated bytes
                 * already look like the prefix, which keeps every "ERROR: 3" and
                 * "[$][SKT][EVENT]: ..." out of the parse. */
                if (c == ':' && p->line_len > DATA_PREFIX_LEN &&
                    memcmp(p->line, DATA_PREFIX, DATA_PREFIX_LEN) == 0)
                {
                    uint8_t  link  = 0;
                    uint32_t total = 0;

                    if (parse_data_head(p->line, p->line_len, &link, &total))
                    {
                        p->stats.data_events++;
                        p->data_link  = link;
                        p->data_total = total;
                        p->data_done  = 0;
                        p->line_len   = 0;
                        p->line_ovf   = false;

                        if (total == 0U)
                        {
                            /* A zero-length event is legal on the wire and means
                             * nothing to a reassembler, so it is counted and its
                             * CRLF is eaten without an empty callback. */
                            p->state = ST_EOL_CR;
                        }
                        else
                        {
                            p->state = ST_DATA;
                        }
                    }
                    else
                    {
                        /* Looked like a head, was not one.  Left in the text stream
                         * on purpose: it will be logged as a line, which is how you
                         * find out what the firmware actually sent. */
                        p->stats.bad_heads++;
                    }
                }
                break;
            }
        }
    }
}

#endif /* CONFIG_SPI_AT_MASTER */
