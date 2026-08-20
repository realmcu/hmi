/**
 * @file    wifi_8711_at_query.c
 * @brief   Send one AT command to the 8711 and log whatever comes back.
 *
 * See the header for why this is asynchronous.  The short version: the 8711
 * owns the clock and polls every ~2 s, so staging is all a caller can do, and
 * the reply lands on the transport thread two polls later.
 *
 * What the sink prints, and why each line is there:
 *
 *   - Slots whose Magic is neither ATMC nor JPGS get a hex dump of the head.
 *     That is the "the link moves bytes but we disagree about the format" case
 *     and it is worth seeing raw -- a decoded view would just hide it.
 *   - POLL packets are counted, not printed.  They arrive every ~2 s forever;
 *     printing each one buries the reply we actually care about.
 *   - A RESPONSE is printed as text, and matched against the sequence we sent
 *     so a stale reply from an earlier command is visibly labelled as such.
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>
#include "wifi_8711.h"
#include "wifi_8711_xfer.h"
#include "wifi_8711_at_query.h"
#include "spi_at_protocol.h"
#include "../protocol/ebadge_log.h"

#if defined(CONFIG_WIFI_8711)

/* Sequence of the most recently staged command, for response matching. */
static uint32_t s_pending_seq;
/* POLLs seen since the last command -- proof of life when no reply comes. */
static uint32_t s_polls;

/*----------------------------------------------------------------------------*
 *  Slot sink -- transport-thread context, so printf is allowed here.
 *
 *  It must still return promptly: B2W stays low for its whole duration, which
 *  back-pressures the 8711.  A few printf lines is acceptable; anything that
 *  blocks is not.
 *----------------------------------------------------------------------------*/
static void at_query_sink(const uint8_t *rx, size_t len)
{
    if (rx == NULL || len < WIFI_8711_HEADER_SIZE)
    {
        return;
    }

    uint32_t magic = spi_at_get_le32(rx);

    if (magic == WIFI_8711_JPG_MAGIC)
    {
        /* Not what we asked for, but proof the link and the peer's framer both
         * work -- worth one line rather than being dropped silently. */
        EBADGE_LOG("wifi8711 at: JPGS slot (stream frame), ignored here");
        return;
    }

    if (magic != WIFI_8711_AT_MAGIC)
    {
        /* Includes the all-zero slot, which is the normal state of a link
         * where MOSI is not wired or the peer's firmware is not running. */
        EBADGE_LOG1("wifi8711 at: unknown magic %08x, head follows",
                    (unsigned)magic);
        EBADGE_LOG_HEX("wifi8711 rx", rx, 32);
        return;
    }

    spi_at_packet_t pkt;
    spi_at_status_t st = spi_at_parse_packet(rx, &pkt);
    if (st != SPI_AT_OK)
    {
        /* CRC is called out separately: it is the difference between "the link
         * is broken" and "the link works but the bytes got corrupted". */
        EBADGE_WARN2("wifi8711 at: ATMC parse failed %d%s", (int)st,
                     (st == SPI_AT_ERR_CRC) ? " (CRC -- bit errors on the bus)"
                     : "");
        EBADGE_LOG_HEX("wifi8711 rx", rx, 32);
        return;
    }

    if (pkt.type == SPI_AT_TYPE_POLL)
    {
        /* Heartbeat.  Count it and stay quiet -- one line every 2 s forever
         * would push the reply we are waiting for out of the log. */
        s_polls++;
        return;
    }

    if (pkt.type != SPI_AT_TYPE_RESPONSE)
    {
        EBADGE_LOG1("wifi8711 at: unexpected ATMC type %u",
                    (unsigned)pkt.type);
        return;
    }

    /* The reply.  Copy it out to get a NUL terminator: the payload is only
     * effectively terminated by the slot's zero padding, and printing a
     * non-terminated pointer would run off into the rest of the slot. */
    char text[192];
    size_t n = spi_at_copy_payload(&pkt, text, sizeof(text));

    EBADGE_LOG("---- 8711 AT RESPONSE ----");
    EBADGE_LOG4("seq=%u (want %u)%s  len=%u",
                (unsigned)pkt.sequence, (unsigned)s_pending_seq,
                (pkt.sequence == s_pending_seq) ? "" : " MISMATCH (stale reply)",
                (unsigned)pkt.length);
    EBADGE_LOG1("polls seen since command: %u", (unsigned)s_polls);
    /* Text first -- it is the answer.  The hex dump after it is for the case
     * where the text looks wrong and the raw bytes settle the argument. */
    EBADGE_LOG1("payload: %s", text);
    EBADGE_LOG_HEX("payload hex", pkt.payload,
                   (pkt.length > 32U) ? 32U : pkt.length);
    if (n < pkt.length)
    {
        EBADGE_LOG2("payload truncated for printing: %u of %u B",
                    (unsigned)n, (unsigned)pkt.length);
    }
    EBADGE_LOG("--------------------------");
}

/*----------------------------------------------------------------------------*
 *  Command staging
 *----------------------------------------------------------------------------*/
static int at_query_send(const char *cmd)
{
    int rc;

    if (!wifi_8711_ready())
    {
        /* wifi_8711_init() failed or was never built in.  Non-fatal at boot by
         * design (main.c), so it is entirely possible to get here. */
        EBADGE_WARN("wifi8711 at: link not initialised (wifi_8711_init failed?)");
        return -ENODEV;
    }

    /* Nothing in main() starts the transport, so the first debug query has to.
     * Without this the command would stage into a transport that never clocks
     * it and we would report success for a byte that never left. */
    if (!wifi_8711_xfer_started())
    {
        rc = wifi_8711_xfer_start(at_query_sink);
        if (rc != 0)
        {
            EBADGE_ERR1("wifi8711 at: xfer_start failed %d", rc);
            return rc;
        }
        EBADGE_LOG1("wifi8711 at: transport started on demand, b2w=%d",
                    wifi_8711_get_ready());
    }
    else
    {
        /* Already running -- most likely started from the shell with a NULL
         * sink, in which case the reply would go nowhere without this. */
        (void)wifi_8711_xfer_set_sink(at_query_sink);
    }

    s_polls = 0U;

    rc = wifi_8711_xfer_test_at(cmd, &s_pending_seq);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711 at: staging failed %d", rc);
        return rc;
    }

    /* Spell out the timing, because "it returned 0 but nothing printed yet" is
     * the expected state for the next couple of seconds, not a failure. */
    EBADGE_LOG2("wifi8711 at: staged \"%s\" seq=%u", cmd,
                (unsigned)s_pending_seq);
    EBADGE_LOG("wifi8711 at: the 8711 owns the clock -- reply in ~2-4 s");
    return 0;
}

int wifi_8711_at_query_ap_info(void)
{
    return at_query_send(SPI_AT_CMD_WLSTATE);
}

int wifi_8711_at_start_ap(void)
{
    return at_query_send(SPI_AT_CMD_WLSTARTAP);
}

#endif /* CONFIG_WIFI_8711 */
