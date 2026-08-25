/*
 * spi_at_protocol.h - SPI AT command tunnel protocol (RTL8773G <-> RTL8711FA)
 *
 * Self-contained, platform-independent helper header for the RTL8773GTP side to
 * build/parse the ATMC packets exchanged with the RTL8711FA AT command firmware
 * over SPI. See doc/spi-at-command-protocol.md for the full protocol spec.
 *
 * Only depends on <stdint.h>, <stddef.h> and <string.h>. All multi-byte fields
 * are little-endian. Every SPI transaction is a fixed 4096-byte slot.
 *
 *   Physical layer : SPI Mode 3, 8-bit, MSB-first, CS active-low, ~20 MHz,
 *                    full-duplex, fixed 4096 bytes per transaction.
 *   Master         : RTL8711FA (drives SCLK/CS, polls every ~1 s when idle).
 *   Slave          : RTL8773GTP (this side; passive, replies on the next poll).
 *
 * The idle beat was 2 s until protocol v3.1 sec.12.6 halved it.  Anything on
 * this side that sizes a timeout off "how long until the next chance to be
 * clocked" is therefore twice as generous as it used to be -- which is what
 * makes a few-second staleness rule on the Wi-Fi state practical.
 *
 * Usage (RTL8773G slave):
 *   1) To send a command, fill a 4096-byte TX slot:
 *          uint32_t seq = spi_at_next_sequence(&my_seq_counter);
 *          spi_at_build_packet(tx_slot, SPI_AT_TYPE_COMMAND, seq, "AT+WLSTATE\r\n");
 *      then arm the SPI slave TX DMA with tx_slot and wait for the master poll.
 *   2) On every received 4096-byte slot, parse it:
 *          spi_at_packet_t pkt;
 *          if (spi_at_parse_packet(rx_slot, &pkt) == SPI_AT_OK &&
 *              pkt.type == SPI_AT_TYPE_RESPONSE && pkt.sequence == seq) {
 *              // pkt.payload / pkt.length hold the response text
 *          }
 *
 * SPDX-License-Identifier: MIT-0 (use freely in customer firmware)
 */
#ifndef SPI_AT_PROTOCOL_H
#define SPI_AT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Protocol constants                                                        */
/* ------------------------------------------------------------------------- */

#define SPI_AT_SLOT_SIZE        4096U        /* fixed SPI transaction length  */
#define SPI_AT_MAGIC            0x434D5441U  /* "ATMC", little-endian         */
#define SPI_AT_VERSION          1U

/* Packet types (header byte at offset 5). */
#define SPI_AT_TYPE_COMMAND     1U           /* 8773 -> 8711 (has payload)    */
#define SPI_AT_TYPE_RESPONSE    2U           /* 8711 -> 8773 (has payload)    */

/*
 * 8711 -> 8773 heartbeat.  NOT empty since protocol v3.1 sec.7.1: the payload
 * now carries the whole Wi-Fi state block (sec.7.4), Length is its real byte
 * count and the CRC covers it like any other payload.  Code that hardcoded
 * "POLL length is 0" started failing its checks; spi_at_parse_packet() below is
 * generic and was unaffected, but a receiver that ignores the payload throws
 * away a free 1 Hz status feed.
 */
#define SPI_AT_TYPE_POLL        3U

#define SPI_AT_HEADER_SIZE      32U
#define SPI_AT_PAYLOAD_SIZE     (SPI_AT_SLOT_SIZE - SPI_AT_HEADER_SIZE) /* 4064 */

/* The RTL8711F command parser buffer is 128 bytes: a COMMAND payload (incl.
 * trailing CRLF) must be shorter than this or it is silently dropped. */
#define SPI_AT_MAX_COMMAND_LEN  127U

/* Header field byte offsets within a slot. */
#define SPI_AT_OFF_MAGIC        0U   /* uint32 LE */
#define SPI_AT_OFF_VERSION      4U   /* uint8     */
#define SPI_AT_OFF_TYPE         5U   /* uint8     */
#define SPI_AT_OFF_LENGTH       6U   /* uint16 LE (payload bytes)             */
#define SPI_AT_OFF_SEQUENCE     8U   /* uint32 LE */
#define SPI_AT_OFF_CRC32        12U  /* uint32 LE (CRC of payload only)       */
/* bytes [16..31] reserved, must be zero */

/* Parse result codes. */
typedef enum
{
    SPI_AT_OK              = 0,
    SPI_AT_ERR_MAGIC       = -1, /* bad magic                                 */
    SPI_AT_ERR_VERSION     = -2, /* unsupported version                       */
    SPI_AT_ERR_TYPE        = -3, /* unknown type                              */
    SPI_AT_ERR_LENGTH      = -4, /* length exceeds payload capacity           */
    SPI_AT_ERR_CRC         = -5, /* payload CRC mismatch                      */
    SPI_AT_ERR_ARG         = -6  /* NULL argument                             */
} spi_at_status_t;

/* Parsed packet view. `payload` points into the caller's slot buffer; it is
 * NUL-terminated in the caller-provided scratch only if you copy it out. Here
 * we expose a pointer + length into the original slot (payload is not
 * guaranteed NUL-terminated inside the slot, but the firmware zero-pads the
 * rest of the slot so a text payload shorter than the slot is effectively
 * NUL-terminated). */
typedef struct
{
    uint8_t         version;
    uint8_t         type;
    uint16_t        length;             /* payload byte count                */
    uint32_t        sequence;
    const uint8_t  *payload;            /* -> slot + SPI_AT_HEADER_SIZE       */
} spi_at_packet_t;

/* ------------------------------------------------------------------------- */
/* Little-endian helpers                                                     */
/* ------------------------------------------------------------------------- */

static inline uint16_t spi_at_get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t spi_at_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void spi_at_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static inline void spi_at_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

/* ------------------------------------------------------------------------- */
/* CRC-32 (reflected IEEE 802.3, init 0xFFFFFFFF, final XOR 0xFFFFFFFF).      */
/* Matches zlib crc32. MUST be identical on both ends.                       */
/* ------------------------------------------------------------------------- */

static inline uint32_t spi_at_crc32(const uint8_t *data, size_t size)
{
    static const uint32_t table[16] =
    {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
    };
    uint32_t crc = 0xFFFFFFFFU;

    while (size-- != 0U)
    {
        crc ^= *data++;
        crc = (crc >> 4) ^ table[crc & 0x0FU];
        crc = (crc >> 4) ^ table[crc & 0x0FU];
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ------------------------------------------------------------------------- */
/* Packet build / parse                                                      */
/* ------------------------------------------------------------------------- */

/*
 * Build one 4096-byte ATMC slot into `slot`.
 *
 *   slot  : caller buffer of exactly SPI_AT_SLOT_SIZE (4096) bytes.
 *   type  : SPI_AT_TYPE_COMMAND / _RESPONSE / _POLL.
 *   seq   : sequence number (see spi_at_next_sequence()).
 *   text  : payload string (may be NULL for POLL). Copied verbatim; include a
 *           trailing "\r\n" for AT commands.
 *
 * Returns the payload length actually written (0..SPI_AT_PAYLOAD_SIZE-1), or a
 * negative spi_at_status_t on error. The whole slot is zero-padded first, so
 * unused bytes are 0.
 *
 * NOTE: for COMMAND packets keep the payload (incl. CRLF) < SPI_AT_MAX_COMMAND_LEN
 * (128) bytes, otherwise the RTL8711F parser drops it.
 */
static inline int spi_at_build_packet(uint8_t *slot, uint8_t type,
                                      uint32_t seq, const char *text)
{
    size_t length;

    if (slot == NULL)
    {
        return SPI_AT_ERR_ARG;
    }
    length = (text == NULL) ? 0U : strlen(text);
    if (length > (size_t)(SPI_AT_PAYLOAD_SIZE - 1U))
    {
        length = (size_t)(SPI_AT_PAYLOAD_SIZE - 1U);
    }

    memset(slot, 0, SPI_AT_SLOT_SIZE);
    spi_at_put_le32(slot + SPI_AT_OFF_MAGIC, SPI_AT_MAGIC);
    slot[SPI_AT_OFF_VERSION] = SPI_AT_VERSION;
    slot[SPI_AT_OFF_TYPE]    = type;
    spi_at_put_le16(slot + SPI_AT_OFF_LENGTH, (uint16_t)length);
    spi_at_put_le32(slot + SPI_AT_OFF_SEQUENCE, seq);
    if (length != 0U)
    {
        memcpy(slot + SPI_AT_HEADER_SIZE, text, length);
    }
    spi_at_put_le32(slot + SPI_AT_OFF_CRC32,
                    spi_at_crc32(slot + SPI_AT_HEADER_SIZE, length));
    return (int)length;
}

/*
 * Parse and validate one received 4096-byte ATMC slot.
 *
 *   slot : caller buffer of SPI_AT_SLOT_SIZE bytes (as received over SPI).
 *   out  : filled with the decoded header + payload pointer on success.
 *
 * Returns SPI_AT_OK on a valid packet, or a negative spi_at_status_t. The CRC
 * is verified over the payload region only.
 */
static inline spi_at_status_t spi_at_parse_packet(const uint8_t *slot,
                                                  spi_at_packet_t *out)
{
    uint16_t length;
    uint32_t crc;

    if (slot == NULL || out == NULL)
    {
        return SPI_AT_ERR_ARG;
    }
    if (spi_at_get_le32(slot + SPI_AT_OFF_MAGIC) != SPI_AT_MAGIC)
    {
        return SPI_AT_ERR_MAGIC;
    }
    if (slot[SPI_AT_OFF_VERSION] != SPI_AT_VERSION)
    {
        return SPI_AT_ERR_VERSION;
    }
    switch (slot[SPI_AT_OFF_TYPE])
    {
    case SPI_AT_TYPE_COMMAND:
    case SPI_AT_TYPE_RESPONSE:
    case SPI_AT_TYPE_POLL:
        break;
    default:
        return SPI_AT_ERR_TYPE;
    }
    length = spi_at_get_le16(slot + SPI_AT_OFF_LENGTH);
    if (length > SPI_AT_PAYLOAD_SIZE)
    {
        return SPI_AT_ERR_LENGTH;
    }
    crc = spi_at_get_le32(slot + SPI_AT_OFF_CRC32);
    if (crc != spi_at_crc32(slot + SPI_AT_HEADER_SIZE, length))
    {
        return SPI_AT_ERR_CRC;
    }

    out->version  = slot[SPI_AT_OFF_VERSION];
    out->type     = slot[SPI_AT_OFF_TYPE];
    out->length   = length;
    out->sequence = spi_at_get_le32(slot + SPI_AT_OFF_SEQUENCE);
    out->payload  = slot + SPI_AT_HEADER_SIZE;
    return SPI_AT_OK;
}

/* ------------------------------------------------------------------------- */
/* Convenience helpers                                                       */
/* ------------------------------------------------------------------------- */

/*
 * Return the next command sequence number, skipping 0 (0 is reserved for POLL).
 * `counter` is caller-owned state; initialise it to 0.
 */
static inline uint32_t spi_at_next_sequence(uint32_t *counter)
{
    uint32_t s = *counter + 1U;
    if (s == 0U)
    {
        s = 1U;
    }
    *counter = s;
    return s;
}

/* True if the parsed packet is a RESPONSE matching the given command sequence. */
static inline int spi_at_is_response_for(const spi_at_packet_t *pkt, uint32_t seq)
{
    return pkt != NULL && pkt->type == SPI_AT_TYPE_RESPONSE &&
           pkt->sequence == seq;
}

/*
 * Copy the payload into a caller buffer and NUL-terminate it (safe text access).
 *
 *   dst      : destination buffer.
 *   dst_size : size of dst in bytes (must be >= 1).
 * Returns the number of payload bytes copied (may be truncated to dst_size-1).
 */
static inline size_t spi_at_copy_payload(const spi_at_packet_t *pkt,
                                         char *dst, size_t dst_size)
{
    size_t n;

    if (pkt == NULL || dst == NULL || dst_size == 0U)
    {
        return 0U;
    }
    n = pkt->length;
    if (n > dst_size - 1U)
    {
        n = dst_size - 1U;
    }
    memcpy(dst, pkt->payload, n);
    dst[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------------- */
/* Standard AT command strings                                               */
/* ------------------------------------------------------------------------- */

#define SPI_AT_CMD_WLSTATE      "AT+WLSTATE\r\n"   /* query SoftAP state      */
#define SPI_AT_CMD_WLSTARTAP    "AT+WLSTARTAP\r\n" /* start the SoftAP        */

/* ------------------------------------------------------------------------- */
/* The Wi-Fi state block (sec.7.4)                                           */
/*                                                                           */
/* ONE text with THREE carriers, which is the fact the whole Wi-Fi state      */
/* handling on this side is built around:                                     */
/*                                                                           */
/*   - the POLL payload            (~1 Hz, no terminator line)                */
/*   - the AT+WLSTATE response     (same block + "[+WLSTATE]:OK"/":ERROR")    */
/*   - an unsolicited notification (same block + "[+WLSTATE]:OK", pushed once */
/*                                  when a requested start succeeds)          */
/*                                                                           */
/* So one parser serves all three, and the difference between them is only    */
/* which line, if any, is appended.                                          */
/*                                                                           */
/* The LAYOUT IS FIXED: every key appears every time, in this order, whatever */
/* the AP state.  "No value" is expressed as a zero value, never as a missing */
/* line -- so a parser must not branch on the state before reading the keys.  */
/*                                                                           */
/*   AP=UP|STARTING|DOWN                                                     */
/*   SSID=<...>          PASSWORD=<...>    CHANNEL=<n>                        */
/*   IP=<a.b.c.d>        PORT=<n>          FILE_PORT=<n>                      */
/*   CLIENTS=<n>                                                             */
/*   CLIENT=1 MAC=.. IP=.. RSSI=..    (0..CLIENTS lines; CLIENTS= is the only */
/*                                     authoritative count, do not count      */
/*                                     lines.  A just-associated STA shows    */
/*                                     IP=0.0.0.0 until DHCP -- associated    */
/*                                     but not yet leased, not a failure.)    */
/* ------------------------------------------------------------------------- */

/** First line of the block, and the only tri-state in it. */
#define SPI_AT_KEY_AP           "AP="

/** No one has requested the AP yet.  Since v3.1 sec.12.2 the 8711 does NOT
 *  self-start it, so this state persists until we send AT+WLSTARTAP -- both
 *  sides waiting for the other is exactly what it looked like in the field. */
#define SPI_AT_AP_DOWN          "DOWN"

/** The request was accepted and the bring-up (or its 5 s retry) is running.
 *  Wait; do NOT re-send WLSTARTAP.  A real bring-up takes seconds to tens of
 *  seconds. */
#define SPI_AT_AP_STARTING      "STARTING"

/** The credentials in the same block are real and usable. */
#define SPI_AT_AP_UP            "UP"

/** Terminator lines of an AT+WLSTATE reply (sec.10.1).  ":ERROR" here means
 *  "the AP is not available right now" and is deliberately distinguishable
 *  from the bare "[AT]:ERROR" that means the command was not even matched --
 *  the block itself is present either way. */
#define SPI_AT_RSP_WLSTATE_OK       "[+WLSTATE]:OK"
#define SPI_AT_RSP_WLSTATE_ERROR    "[+WLSTATE]:ERROR"

/*
 * Expected line from AT+WLSTARTAP (sec.10.2).
 *
 * ":OK" means THE REQUEST WAS ACCEPTED, not that the AP is up -- a bring-up
 * takes seconds to tens of seconds and the 8711 answers immediately so as not
 * to freeze the SPI control link while it runs.  Which of the two it is comes
 * from the STATE= line below.
 *
 * Since v3.1 sec.12.8 the reply carries NO credentials.  It used to repeat
 * SSID= and PASSWORD=, and this side ignored both on the grounds that WLSTATE
 * is the one complete source; the vendor then removed them for the same
 * reason from the other end -- the SSID is derived from the MAC at bring-up
 * time, so a copy emitted at REQUEST time may describe a value that does not
 * exist yet.
 *
 * Failure is the shared "[AT]:ERROR" -- unlike WLSTOPAP there is no
 * command-specific error line.
 */
#define SPI_AT_RSP_WLSTARTAP_OK     "[+WLSTARTAP]:OK"

/** Lifecycle line following the OK.  UP = already running (the call was
 *  redundant), STARTING = bring-up now in progress. */
#define SPI_AT_RSP_STARTAP_STATE_UP        "STATE=UP"
#define SPI_AT_RSP_STARTAP_STATE_STARTING  "STATE=STARTING"

/*
 * Take the SoftAP down.
 *
 * The counterpart to WLSTARTAP, and the first command on this surface that
 * actually CHANGES the radio's state rather than reporting it -- so unlike the
 * two above it is not idempotent, and unlike them its reply carries no fields.
 *
 *   [+WLSTOPAP]:OK      the AP is down.
 *   [+WLSTOPAP]:ERROR   the AP was not running to begin with, OR a start is
 *                       still in progress.  Those two are not distinguishable
 *                       from the reply, which is why callers must not read
 *                       ERROR as "the AP is still up" -- see the note in
 *                       wifi_8711_at_ap_stop().
 *
 * Note the failure line is command-specific ("[+WLSTOPAP]:ERROR"), not the
 * shared "[AT]:ERROR" that an unrecognised command returns.  Both have to be
 * treated as a refusal: against an 8711 build predating this command it is the
 * generic one that comes back.
 */
#define SPI_AT_CMD_WLSTOPAP     "AT+WLSTOPAP\r\n"  /* stop the SoftAP         */

/** Expected lines from AT+WLSTOPAP. */
#define SPI_AT_RSP_WLSTOPAP_OK      "[+WLSTOPAP]:OK"
#define SPI_AT_RSP_WLSTOPAP_ERROR   "[+WLSTOPAP]:ERROR"

/*
 * File-transfer control, added in protocol v2.2 (sec.10.3 / 10.4).  Both act on
 * the port-9000 file connection and answer ERROR when there is no active one.
 *
 * These exist because v2.2 moved EBXR generation from the 8711 to us: the 8711
 * forwards the last EBFS slot and then *waits*, holding the TCP connection open
 * for up to 120 s, for one of these two commands.  Neither is optional -- a
 * session that sends neither ends with the 8711 closing on its own timeout and
 * the phone seeing a bare disconnect instead of a coded result.
 *
 *   AT+XFERACK=<status>,<reason>   8711 builds the EBXR and closes the TCP.
 *                                  status 1=success / 0=failure, reason is an
 *                                  eBadge sec.2.6 transfer code (0 on success).
 *                                  Both are DECIMAL uint8 text, not hex.
 *   AT+XFERSTOP                    shutdown now, stop forwarding, NO EBXR.
 *
 * The ACK prefix is kept separate from the CRLF because the arguments go
 * between them; wifi_8711_at_xfer.c is the only place that should format it.
 */
#define SPI_AT_CMD_XFERACK_PREFIX "AT+XFERACK="
#define SPI_AT_CMD_XFERSTOP       "AT+XFERSTOP\r\n"

/** Expected success lines.  Failure is the shared "[AT]:ERROR". */
#define SPI_AT_RSP_XFERACK_OK     "[+XFERACK]:OK"
#define SPI_AT_RSP_XFERSTOP_OK    "[+XFERSTOP]:OK"

#ifdef __cplusplus
}
#endif

#endif /* SPI_AT_PROTOCOL_H */
