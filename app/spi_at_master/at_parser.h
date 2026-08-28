/**
 * @file    at_parser.h
 * @brief   Byte stream -> semantic units, for Realtek's standard AT firmware.
 *
 * The one layer between "16 KiB slot arrived" and "a command finished".  It
 * knows nothing about commands, Wi-Fi or sockets; it only splits the inbound
 * byte stream into the two things that stream is made of:
 *
 *   TEXT LINES      "OK", "ERROR: 3", "MODE: AP", ">>>", "ATCMD READY",
 *                   "[$]client connected:\"aa:bb:..\"" -- CRLF terminated
 *   BINARY SEGMENTS the payload of a [$][SKT][DATA] event, which is raw TCP
 *
 * ---------------------------------------------------------------------------
 * WHY THIS CANNOT BE A LINE BUFFER
 * ---------------------------------------------------------------------------
 * The socket data event looks like a line but is not one:
 *
 *      [$][SKT][DATA][3][1460]:<1460 raw bytes><CR><LF>
 *
 * Those 1460 bytes are whatever the phone sent.  A JPEG contains CR, LF, CR LF
 * and 0x00 in quantity, so anything that splits on '\n' or stops at '\0' will
 * cut a packet in half and then resynchronise on a byte that is not a header.
 * The only correct reading is: match the ASCII head up to ':', take <len> from
 * it, then read EXACTLY len bytes without looking at them, then eat the
 * trailing CRLF.  That is the entire reason this file exists.
 *
 * ---------------------------------------------------------------------------
 * A DATA EVENT CAN SPAN TWO SLOTS, SO THE PAYLOAD IS DELIVERED IN CHUNKS
 * ---------------------------------------------------------------------------
 * The 8711 caps one event's payload at AT_SOCKET_RECEIVE_BUFFER_SIZE = 16384,
 * which is the same number as the SPI slot -- but the event also carries ~25 B
 * of ASCII head plus CRLF, and a slot's payload area is only 16376 B.  So a
 * maximum-size event does NOT fit in one slot and the tail arrives in the next
 * one.
 *
 * Rather than buffer 16 KiB here to hide that, on_data() is called once per
 * contiguous run with (offset, total, is_last).  The consumer is a stream
 * reassembler anyway -- it wants chunks -- and this keeps the parser's own
 * footprint at one line buffer.
 *
 * ---------------------------------------------------------------------------
 * EVENTS INTERLEAVE WITH RESPONSES.  THAT IS NORMAL, NOT AN ERROR
 * ---------------------------------------------------------------------------
 * at_printf_indicate() on the 8711 side emits [$] lines from whichever thread
 * produced them, so one can land between two lines of a multi-line command
 * response.  at_printf_lock()/unlock() only guarantees a single event is not
 * itself split.  This parser therefore makes no distinction: it reports lines
 * in arrival order and leaves "is this mine or unsolicited?" to at_client,
 * which is the layer that knows whether a command is in flight.
 *
 * CONTEXT: at_parser_feed() is called from the SPI transport thread, inside the
 * RX callback, with M2S still asserted -- so the callbacks below back-pressure
 * the 8711 for as long as they run.  Keep them short; do not block in them.
 */
#ifndef _AT_PARSER_H_
#define _AT_PARSER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest ASCII line kept whole.
 *
 *  Sized off the longest thing the standard firmware actually emits: the
 *  connect event
 *      [$][SKT][EVENT]: A client[link_id:3,seed,tcp,dst_address:192.168.43.2,
 *      dst_port:54321] connected to server[link_id:0]
 *  is about 110 B, and an AT+WLSTATE CLIENT= line with a MAC and an IP is
 *  shorter still.  256 leaves room without putting a second KiB on the
 *  transport thread's 2 KiB stack -- the parser instance is static, not
 *  automatic, but the figure is chosen with that in mind.
 *
 *  Overflow is survivable and counted, never fatal: the excess is dropped, the
 *  line is still terminated on its CRLF, and the framing stays in step.  This
 *  matters because losing framing on an over-long *text* line would corrupt the
 *  next *binary* one. */
#define AT_PARSER_LINE_MAX      256

/** Largest payload one [$][SKT][DATA] event may declare.
 *
 *  The 8711's AT_SOCKET_RECEIVE_BUFFER_SIZE, which its own config ties to
 *  ATCMD_SPI_DMA_SIZE -- so this is the SPI slot size, not a coincidence.  Used
 *  as a sanity bound when parsing <len>: a head claiming more than this was
 *  mis-parsed, and accepting it would make the parser skip real response text as
 *  though it were payload. */
#define AT_PARSER_DATA_MAX      16384U

/**
 * @brief  One complete text line, NUL-terminated, CR/LF already stripped.
 *
 * @param  line  never NULL; may be the empty string (a bare CRLF, which the
 *               firmware emits between response blocks)
 * @param  len   strlen(line), passed so callers need not recount
 */
typedef void (*at_parser_line_cb_t)(const char *line, uint16_t len, void *user);

/**
 * @brief  A contiguous run of one [$][SKT][DATA] event's payload.
 *
 * @param  link_id  the connection the bytes came from -- for a server that has
 *                  accepted a client this is the CLIENT's link (the "seed"),
 *                  not the listening link
 * @param  chunk    raw TCP bytes.  Valid only for the duration of the call:
 *                  it points into the SPI slot, which the next transaction
 *                  overwrites.
 * @param  offset   where @p chunk starts within the event's payload
 * @param  total    the event's declared payload length
 * @param  is_last  true when offset + len == total, i.e. this call completes
 *                  the event
 */
typedef void (*at_parser_data_cb_t)(uint8_t link_id, const uint8_t *chunk,
                                    uint16_t len, uint32_t offset,
                                    uint32_t total, bool is_last, void *user);

typedef struct
{
    at_parser_line_cb_t on_line;
    at_parser_data_cb_t on_data;
    void               *user;
} at_parser_cbs_t;

/** Counters -- all failure modes separated, because they have different causes.
 *
 *  A nonzero `desyncs` is the interesting one: it means a data event's trailing
 *  CRLF was not where <len> said it would be, which is either a length field we
 *  mis-parsed or a slot we lost.  Everything after such an event is suspect. */
typedef struct
{
    uint32_t lines;        /**< text lines emitted                            */
    uint32_t data_events;  /**< [$][SKT][DATA] heads accepted                 */
    uint32_t data_bytes;   /**< payload bytes forwarded to on_data             */
    uint32_t line_ovf;     /**< lines longer than AT_PARSER_LINE_MAX          */
    uint32_t bad_heads;    /**< looked like a DATA head but did not parse      */
    uint32_t desyncs;      /**< no CRLF where the payload ended                */
} at_parser_stats_t;

/** Parser state.  Allocate one statically per byte stream; there is exactly one
 *  stream here, but keeping it a struct is what lets a host-side test feed a
 *  captured buffer through it with no hardware. */
typedef struct
{
    at_parser_cbs_t cbs;

    uint8_t  state;                     /* private, see at_parser.c            */
    char     line[AT_PARSER_LINE_MAX];
    uint16_t line_len;
    bool     line_ovf;

    uint8_t  data_link;
    uint32_t data_total;
    uint32_t data_done;

    at_parser_stats_t stats;
} at_parser_t;

/** Install callbacks and clear state.  @p cbs is copied. */
void at_parser_init(at_parser_t *p, const at_parser_cbs_t *cbs);

/** Drop any partial line or partial data payload and start clean.
 *
 *  Call this when the link itself restarts (a fresh "ATCMD READY").  Do NOT
 *  call it to recover from a bad line: the stream is a stream, and discarding a
 *  half-read payload mid-event is how a desync becomes permanent. */
void at_parser_reset(at_parser_t *p);

/** Feed one slot's worth of received bytes. */
void at_parser_feed(at_parser_t *p, const uint8_t *buf, uint16_t len);

/** True while the parser is mid-payload of a data event.
 *
 *  at_client uses this to keep a command timeout from firing in the middle of a
 *  16 KiB inbound burst that has nothing to do with that command. */
bool at_parser_in_data(const at_parser_t *p);

void at_parser_get_stats(const at_parser_t *p, at_parser_stats_t *out);

/**
 * @brief  Is @p line one of the firmware's unsolicited "[$]..." indications?
 *
 * A one-line test kept here rather than in at_client so the prefix is spelled
 * out in exactly one place.  Everything else -- "OK", "ERROR: n", ">>>", a
 * response body line -- belongs to whatever command is in flight.
 */
bool at_parser_line_is_event(const char *line, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _AT_PARSER_H_ */
