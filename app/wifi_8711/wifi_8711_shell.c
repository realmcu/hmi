/**
 * @file    wifi_8711_shell.c
 * @brief   `wifi8711` shell group -- bring-up probes for the SPI slave link.
 *
 * These commands exist to answer one question in order, without any parser in
 * the picture: does a 4096-byte slot cross the wire, and are the bytes the ones
 * we sent?
 *
 * The bring-up sequence, with a scope on P4_1 / P2_7:
 *   wifi8711 init        pads configured, B2W low, W2B interrupt armed
 *   wifi8711 info        does the 8711 already drive W2B?  are edges arriving?
 *   wifi8711 b2w 1       raw pad check.  A LIE to the 8711 -- no DMA is armed --
 *                        so use it only to tell "our pad is misconfigured" from
 *                        "the chip is not answering", never as a stand-in for
 *                        the handshake.  Run `xfer` afterwards, which re-takes
 *                        ownership of the line.
 *   wifi8711 xfer        start the real transport (thread + first slot armed)
 *   wifi8711 pattern 1   stage a recognisable TX slot
 *   wifi8711 wait 5000   block until a slot arrives -- the actual pass/fail
 *   wifi8711 stats       which half is broken, if it is broken
 *   wifi8711 rx 64       look at what the 8711 actually sent us
 *   wifi8711 at state    first real ATMC exchange (needs 8711 AT firmware)
 *
 * There is no loopback command and there cannot be one: we are the SPI slave
 * and cannot generate a clock, so nothing moves on this bus unless the 8711
 * drives it.  Every command above needs a live peer.  A self-test that passed
 * without one would be worse than no test at all.
 */
#if defined(CONFIG_WIFI_8711) && defined(CONFIG_WIFI_8711_TEST)

#include <zephyr/shell/shell.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "wifi_8711.h"
#include "wifi_8711_xfer.h"
#include "wifi_8711_at.h"     /* WIFI_8711_AT_TIMEOUT_MS, for the margin check */
#include "wifi_8711_at_query.h"
#include "wifi_8711_at_xfer.h"
#include "wifi_8711_at_ap.h"  /* apstop goes through the real transaction layer */
#include "spi_at_protocol.h"
#include "../protocol/ebadge_log.h"   /* EBADGE_HEXDUMP_DEFAULT, for the banner */

#define RES_NODE  DT_NODELABEL(wifi_8711_resources)
#define SPI_NODE  DT_NODELABEL(spi0_slave)

static const struct gpio_dt_spec b2w_gpio = GPIO_DT_SPEC_GET(RES_NODE, b2w_gpios);
static const struct gpio_dt_spec w2b_gpio = GPIO_DT_SPEC_GET(RES_NODE, w2b_gpios);

static int cmd_init(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int rc = wifi_8711_init();
    shell_print(sh, "wifi_8711_init() = %d (%s)", rc,
                (rc == 0) ? "ok" : "failed");
    return rc;
}

/* Dumps what the overlay actually produced -- the fastest way to tell a
 * silently-wrong pin assignment from a wiring problem. */
static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint32_t rising = 0, falling = 0;

    shell_print(sh, "role        : SLAVE (8711 drives SCLK/CS)");
    shell_print(sh, "ready       : %s", wifi_8711_ready() ? "yes" : "no");
    shell_print(sh, "transport   : %s", wifi_8711_xfer_started() ? "running" : "stopped");
    /* No spi-max-frequency to print: the 8711 supplies the clock, so the DT
     * node carries no frequency at all (see the overlay's "deliberately
     * absent" list).  Slot geometry is the fixed part worth showing. */
    shell_print(sh, "slot        : %u B fixed, mode3 8bit MSB",
                (unsigned)WIFI_8711_SLOT_SIZE);
    shell_print(sh, "b2w (out)   : port=%s pin=%d", b2w_gpio.port->name,
                (int)b2w_gpio.pin);
    shell_print(sh, "w2b (in)    : port=%s pin=%d", w2b_gpio.port->name,
                (int)w2b_gpio.pin);

    if (wifi_8711_ready())
    {
        wifi_8711_get_w2b_stats(&rising, &falling);
        shell_print(sh, "b2w level   : %d (logical, 1 = armed, send me a slot)",
                    wifi_8711_get_ready());
        shell_print(sh, "w2b level   : %d (logical, 1 = slot requested)",
                    wifi_8711_get_request());
        shell_print(sh, "w2b edges   : rising=%u falling=%u",
                    (unsigned)rising, (unsigned)falling);
    }
    return 0;
}

/* Manual B2W drive: with a scope on P4_1 this separates "our pad config is
 * wrong" from "the chip is not answering".  Raising it here is a lie to the
 * 8711 -- no DMA is armed -- so use it only for pad and answer-latency checks,
 * never as a substitute for the real four-phase handshake. */
static int cmd_b2w(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2)
    {
        shell_error(sh, "usage: wifi8711 b2w <0|1>");
        return -EINVAL;
    }
    if (!wifi_8711_ready())
    {
        shell_error(sh, "run `wifi8711 init` first");
        return -ENODEV;
    }
    if (wifi_8711_xfer_started())
    {
        /* The transport owns this line once it is running; a manual poke would
         * desynchronise the rendezvous rather than measure anything. */
        shell_error(sh, "transport running -- b2w is owned by it, refusing");
        return -EBUSY;
    }

    int val = atoi(argv[1]);
    int rc  = wifi_8711_set_ready(val != 0);
    shell_print(sh, "b2w <- %d (logical), rc=%d", val, rc);
    return rc;
}

/* Start the transport.  No sink is registered: at this stage counting slots and
 * dumping bytes is exactly what we want, and a parser would only add a second
 * suspect when something looks wrong. */
static int cmd_xfer(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int rc = wifi_8711_xfer_start(NULL);
    if (rc != 0)
    {
        shell_error(sh, "wifi_8711_xfer_start() = %d", rc);
        return rc;
    }
    shell_print(sh, "transport started: first slot armed, b2w=%d",
                wifi_8711_get_ready());
    shell_print(sh, "next: `wifi8711 wait 5000` then `wifi8711 stats`");
    return 0;
}

static int cmd_pattern(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t seed = (argc == 2) ? (uint32_t)strtoul(argv[1], NULL, 0) : 1U;

    int rc = wifi_8711_xfer_test_pattern(seed);
    if (rc != 0)
    {
        shell_error(sh, "stage failed %d (transport running?)", rc);
        return rc;
    }
    /* Staged, not sent: the 8711 owns the clock, so it goes out on whichever
     * slot the 8711 chooses to run next -- about a second away at the idle POLL
     * rate (sec.12.6 halved the old 2 s beat). */
    shell_print(sh, "staged \"8773TEST\" + seed=%u + ramp; goes out on the next slot",
                (unsigned)seed);
    return 0;
}

static int cmd_idle(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int rc = wifi_8711_xfer_set_tx_idle();
    shell_print(sh, "tx <- all zeroes, rc=%d", rc);
    return rc;
}

static int cmd_at(const struct shell *sh, size_t argc, char **argv)
{
    const char *text;
    uint32_t    seq = 0;
    int         rc;

    if (argc != 2)
    {
        shell_error(sh, "usage: wifi8711 at <state|startap>");
        return -EINVAL;
    }

    /* Only the two read-only commands are offered here.  AT+WLSTOPAP exists too
     * (sec.10.2) but has a dedicated subcommand: its reply is a verdict that wants
     * interpreting rather than a slot to hexdump, and switching the radio off is a
     * side effect that does not belong on a raw framing probe.  Anything else comes
     * back as [AT]:ERROR, so there is no point in accepting free text here and
     * pretending otherwise. */
    if (strcmp(argv[1], "state") == 0)
    {
        text = SPI_AT_CMD_WLSTATE;
    }
    else if (strcmp(argv[1], "startap") == 0)
    {
        text = SPI_AT_CMD_WLSTARTAP;
    }
    else
    {
        shell_error(sh, "unknown command; `state` and `startap` here, `apstop` for"
                    " AT+WLSTOPAP");
        return -EINVAL;
    }

    rc = wifi_8711_xfer_test_at(text, &seq);
    if (rc != 0)
    {
        shell_error(sh, "stage failed %d (transport running?)", rc);
        return rc;
    }

    shell_print(sh, "staged ATMC COMMAND seq=%u", (unsigned)seq);
    /* Two POLL periods minimum: one to carry the COMMAND out, one to bring the
     * RESPONSE back (sec.1.1).  That floor is the protocol's, not ours. */
    shell_print(sh, "expect the RESPONSE two POLLs later (~2-4 s):");
    shell_print(sh, "  wifi8711 wait 5000 && wifi8711 rx 128");
    return 0;
}

static int cmd_wait(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t ms = (argc == 2) ? (uint32_t)strtoul(argv[1], NULL, 0) : 5000U;

    int rc = wifi_8711_xfer_test_wait_slot(ms);
    if (rc != 0)
    {
        /* The expected result against a peer whose AT firmware is not running,
         * so say what to look at rather than just failing. */
        shell_error(sh, "no slot in %u ms", (unsigned)ms);
        shell_print(sh, "check: `info` for W2B edges (pad + irq alive?),");
        shell_print(sh, "       `stats` for arm_fail / slots_err (our side?)");
        return rc;
    }
    shell_print(sh, "slot arrived");
    return 0;
}

static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    wifi_8711_xfer_stats_t st;
    wifi_8711_xfer_get_stats(&st);

    shell_print(sh, "arms        : %u  (arm_fail=%u)",
                (unsigned)st.arms, (unsigned)st.arm_fail);
    shell_print(sh, "slots_ok    : %u  (full %u B)",
                (unsigned)st.slots_ok, (unsigned)WIFI_8711_SLOT_SIZE);
    shell_print(sh, "slots_short : %u  (completed but < slot -> mode/word size)",
                (unsigned)st.slots_short);
    shell_print(sh, "slots_err   : %u  (driver returned negative)",
                (unsigned)st.slots_err);
    shell_print(sh, "rx_nonzero  : %u  (of the ok ones, not all-zero)",
                (unsigned)st.rx_nonzero);
    shell_print(sh, "w2b_wait_to : %u  (W2B stuck high past 1000 ms)",
                (unsigned)st.w2b_wait_to);
    /* Worth spelling out: on a slave a *successful* completion reports the
     * received frame count, so 4096 is the good value and 0 would be bad. */
    shell_print(sh, "last_result : %d  (slave success == %u, not 0)",
                (int)st.last_result, (unsigned)WIFI_8711_SLOT_SIZE);

    if (st.slots_ok != 0U && st.rx_nonzero == 0U)
    {
        shell_warn(sh, "slots complete but RX is all zero -> check MOSI wiring");
    }

    /*------------------------------------------------------------------------*
     *  Handshake phases (sec.3.1 phases 4..6)
     *
     *  Printed together with the W2B edge counts on purpose: neither block
     *  identifies a phase slip on its own.  The 8711 reports every one of these
     *  as the same "READY timeout after 1000 ms", so the whole point of this
     *  section is to say WHICH of them is happening.
     *------------------------------------------------------------------------*/
    uint32_t rising = 0U, falling = 0U;
    wifi_8711_get_w2b_stats(&rising, &falling);

    shell_print(sh, "--- handshake ---");
    shell_print(sh, "w2b edges   : rising=%u falling=%u",
                (unsigned)rising, (unsigned)falling);
    shell_print(sh, "w2b_at_start: %d  (1 = 8711 was already requesting)",
                (int)st.w2b_at_start);
    shell_print(sh, "arm_w2b_high: %u  (armed while W2B still high -> phase slip)",
                (unsigned)st.arm_w2b_high);
    shell_print(sh, "rxdone_w2b_l: %u  (W2B already low in RX ISR -> early release)",
                (unsigned)st.rxdone_w2b_low);
    /* Both, not just the max: last says whether it is happening right now, max
     * says whether it ever did.  A max of ~1000200 is a timeout, not slowness. */
    shell_print(sh, "w2b_fall_us : last=%u max=%u  (healthy: single digits)",
                (unsigned)st.w2b_fall_us_last, (unsigned)st.w2b_fall_us_max);

    /* The three readings the 8711's single log line cannot distinguish. */
    if (st.arms == 0U)
    {
        shell_warn(sh, "never armed -> B2W never rose; the 8711 will READY-timeout");
    }
    else if (st.arm_w2b_high > 1U)
    {
        shell_warn(sh, "arm_w2b_high > 1 -> we keep promising READY while the 8711"
                   " still holds the previous slot open (out of phase)");
    }
    if (st.w2b_wait_to != 0U && rising == falling + 1U)
    {
        shell_warn(sh, "W2B asserted and never released -> the 8711 is stuck, not us");
    }
    if (st.rxdone_w2b_low != 0U)
    {
        shell_warn(sh, "W2B released before our B2W fall -> its next rising edge is"
                   " ambiguous (lost-edge race)");
    }

    /*------------------------------------------------------------------------*
     *  Cadence -- the 8711's idle POLL rate, measured
     *
     *  This is the number every upper-layer deadline should be sized from, and
     *  it is NOT the nominal beat in the spec (~1 s since sec.12.6, ~2 s before
     *  it): that figure is what sized the AT timeout at 8 s, and a reply measured
     *  at 10.3 s then failed every time.  The nominal beat is a rate, not a bound
     *  on the round trip.
     *
     *  It is also the number the Wi-Fi state staleness rule in
     *  ebadge_port_softap.c is sized off -- if the measured gap here is regularly
     *  above ~1 s, that 6 s rule will trip on a healthy link and provoke a query
     *  per interval.
     *------------------------------------------------------------------------*/
    shell_print(sh, "--- cadence ---");
    if (st.slot_gaps == 0U)
    {
        /* Say why rather than print three zeroes: one slot is not an interval,
         * and "0 ms" would read as an impossibly fast link. */
        shell_print(sh, "slot_gap    : n/a (need 2 slots, seen %u)",
                    (unsigned)st.slots_ok);
    }
    else
    {
        shell_print(sh, "slot_gap_ms : last=%u min=%u max=%u  (n=%u)",
                    (unsigned)st.slot_gap_ms_last, (unsigned)st.slot_gap_ms_min,
                    (unsigned)st.slot_gap_ms_max, (unsigned)st.slot_gaps);

        /* A reply needs two transactions: one to carry the COMMAND out, one to
         * bring the RESPONSE back, plus the 8711's own turnaround.  So the AT
         * deadline has to clear 2x the WORST gap, not the mean -- sizing it off
         * the mean is exactly how 8000 ms came to fail 100% of the time. */
        unsigned need = (unsigned)st.slot_gap_ms_max * 2U;
        shell_print(sh, "at_timeout  : %u ms vs 2x max gap = %u ms",
                    (unsigned)WIFI_8711_AT_TIMEOUT_MS, need);
        if ((unsigned)WIFI_8711_AT_TIMEOUT_MS < need)
        {
            shell_warn(sh, "AT timeout is below 2x the worst gap -> replies will"
                       " be dropped as stale; raise WIFI_8711_AT_TIMEOUT_MS");
        }
        if (st.slot_gap_ms_max > st.slot_gap_ms_min * 3U)
        {
            /* Jitter matters more than the mean: a deadline that works at the
             * mean and fails at the tail is worse than one that always fails,
             * because it looks like an intermittent link fault. */
            shell_warn(sh, "gap jitter > 3x -> the 8711 has other work competing"
                       " with its idle poll; size deadlines off max, not mean");
        }
    }
    return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    wifi_8711_xfer_reset_stats();
    shell_print(sh, "stats cleared");
    return 0;
}

/* Dump the head of the last received slot.  Tries the ATMC decode first, since
 * a valid header is far more informative than 64 hex bytes -- and a CRC error
 * here is the difference between "the link works" and "the link works but we
 * disagree with the peer about the bytes". */
static int cmd_rx(const struct shell *sh, size_t argc, char **argv)
{
    static uint8_t buf[256];
    size_t want = (argc == 2) ? (size_t)strtoul(argv[1], NULL, 0) : 64U;
    size_t got;

    if (want > sizeof(buf))
    {
        want = sizeof(buf);
    }

    got = wifi_8711_xfer_peek_rx(buf, want);
    if (got == 0U)
    {
        shell_error(sh, "no slot received yet");
        return -EAGAIN;
    }

    shell_print(sh, "magic       : %08x %s",
                (unsigned)spi_at_get_le32(buf),
                (spi_at_get_le32(buf) == WIFI_8711_AT_MAGIC) ? "(ATMC)" :
                (spi_at_get_le32(buf) == WIFI_8711_JPG_MAGIC) ? "(JPGS)" :
                "(neither -- see the hex below)");

    if (spi_at_get_le32(buf) == WIFI_8711_AT_MAGIC)
    {
        static const char *const type_name[] = { "?", "COMMAND", "RESPONSE", "POLL" };
        uint8_t  type = buf[SPI_AT_OFF_TYPE];
        uint16_t len  = spi_at_get_le16(buf + SPI_AT_OFF_LENGTH);

        shell_print(sh, "version/type: %u / %u (%s)", (unsigned)buf[SPI_AT_OFF_VERSION],
                    (unsigned)type, (type <= 3U) ? type_name[type] : "?");
        shell_print(sh, "length/seq  : %u / %u", (unsigned)len,
                    (unsigned)spi_at_get_le32(buf + SPI_AT_OFF_SEQUENCE));

        /* Only decode the text when the whole payload is inside what we copied
         * out -- a truncated CRC check would report a false mismatch. */
        if (len != 0U && (size_t)(SPI_AT_HEADER_SIZE + len) <= got)
        {
            spi_at_packet_t pkt;
            /* Parse over buf, valid because the payload fits in what we copied.
             * spi_at_parse_packet() verifies the CRC over the payload only. */
            spi_at_status_t st = spi_at_parse_packet(buf, &pkt);
            char text[128];

            if (st == SPI_AT_OK)
            {
                (void)spi_at_copy_payload(&pkt, text, sizeof(text));
                shell_print(sh, "payload     : %s", text);
            }
            else
            {
                shell_warn(sh, "parse failed %d%s", (int)st,
                           (st == SPI_AT_ERR_CRC) ? " (CRC -- bit errors on the bus)" : "");
            }
        }
        else if (len != 0U)
        {
            shell_print(sh, "payload     : %u B, re-run with `rx %u` to decode",
                        (unsigned)len, (unsigned)(SPI_AT_HEADER_SIZE + len));
        }
    }

    shell_hexdump(sh, buf, got);
    return 0;
}

/* `rx` shows one slot on demand; this shows every slot as it lands.  Kept as a
 * switch rather than always-on because the dump runs on the transport thread
 * with B2W low, which is affordable at the ~1 s idle POLL rate and a real
 * throttle during a JPEG burst. */
static int cmd_rxdump(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2)
    {
        shell_print(sh, "rxdump is %s -- use: rxdump <on|off>",
                    wifi_8711_xfer_rx_dump() ? "ON" : "off");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0)
    {
        wifi_8711_xfer_set_rx_dump(true);
        shell_print(sh, "rxdump ON: first %u B of every inbound slot, hex + ascii",
                    (unsigned)EBADGE_HEXDUMP_DEFAULT);
        shell_warn(sh, "this delays the next ARM -- turn it off before"
                   " measuring throughput");
        return 0;
    }
    if (strcmp(argv[1], "off") == 0)
    {
        wifi_8711_xfer_set_rx_dump(false);
        shell_print(sh, "rxdump off");
        return 0;
    }

    shell_error(sh, "expected 'on' or 'off'");
    return -EINVAL;
}

/* Same path the BLE 0xFF debug subcmd 0x01 takes, minus the phone: starts the
 * transport if needed, installs the logging sink, stages the query.  Having one
 * shared implementation is the point -- a shell-only variant would let the two
 * drift and then the shell would "work" while the BLE command did not.
 *
 * Worth knowing before reaching for this: since sec.7.1 the same state block
 * arrives unasked on the POLL beat, so this only buys seeing it one beat sooner.
 * What it genuinely tests is whether the link answers a COMMAND -- a separate
 * question from whether the feed is alive. */
static int cmd_apinfo(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int rc = wifi_8711_at_query_ap_info();
    if (rc != 0)
    {
        shell_error(sh, "wifi_8711_at_query_ap_info() = %d%s", rc,
                    (rc == -ENODEV) ? " (wifi_8711_init() never succeeded)" : "");
        return rc;
    }
    shell_print(sh, "AP-info query staged; the reply is printed to the log");
    /* Measured, not derived from the beat: an idle-link round trip came back at
     * ~10.3 s, which is what WIFI_8711_AT_TIMEOUT_MS is sized off.  Saying "~2-4 s"
     * here (the old text) made every normal reply look late. */
    shell_print(sh, "the 8711 owns the clock -- expect it in ~10 s on an idle link");
    return 0;
}

/* Take the AP down from the console (SPI spec sec.10.3).
 *
 * Separate from `at startap` rather than an `at stopap` alias, because this one
 * changes the radio's state instead of describing it, and its reply is a verdict
 * worth printing in words rather than a slot worth hexdumping.
 *
 * In normal operation nothing here is needed: the protocol stack switches the AP
 * off through ebadge_port_softap_shutdown() when the phone disconnects over BLE.
 * This exists to check that half on its own -- watch the next POLL-fed state
 * block report AP=DOWN, which since sec.7.1 needs no query at all. */
static void shell_ap_stop_done(bool ok, void *user)
{
    ARG_UNUSED(user);

    /* Printed to the log, not to `sh`: this lands on the transport thread ~11-13 s
     * later, by which time the shell has long since returned and the struct shell
     * we were called with may belong to a different command. */
    if (ok)
    {
        EBADGE_LOG("wifi8711 shell: AP stopped ([+WLSTOPAP]:OK)");
    }
    else
    {
        EBADGE_LOG("wifi8711 shell: stop refused -- AP was already down, a start is"
                   " in progress, or this 8711 has no WLSTOPAP");
    }
}

static int cmd_apstop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int rc = wifi_8711_at_ap_stop(shell_ap_stop_done, NULL);
    if (rc != 0)
    {
        shell_error(sh, "wifi_8711_at_ap_stop() = %d%s", rc,
                    (rc == -EBUSY) ? " (another AT command outstanding)"
                    : (rc == -ENODEV) ? " (wifi_8711_init() never"
                    " succeeded)" : "");
        return rc;
    }
    shell_print(sh, "AT+WLSTOPAP staged; the verdict is printed to the log");
    /* Worth saying out loud: an ERROR here is not a fault.  The 8711 answers ERROR
     * for an AP that was already down, which is the expected reply on a second
     * run. */
    shell_print(sh, "ERROR is a normal answer if the AP was already down");
    return 0;
}

/* Drive the two transfer-control commands from the console (SPI spec v2.2
 * sec.10).  One command for both because they share a single-flight slot:
 * issuing them separately would mostly demonstrate -EBUSY.
 *
 * With no upload in flight the 8711 answers "[AT]:ERROR", and that is still a
 * useful test -- it proves the framing reaches the peer and comes back as a
 * clean refusal rather than a timeout, which distinguishes "no file connection
 * open" from "the link is dead".
 *
 * In normal operation the firmware sends both by itself, from xfer_session by way
 * of ebadge_port_tcp.  This exists to exercise the 8711 end without a phone. */
static int cmd_xfer_ctrl(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (argc < 2)
    {
        shell_error(sh, "usage: wifi8711 xferctl "
                    "<ack <status> [reason] | stop | stats>");
        return -EINVAL;
    }

    if (strcmp(argv[1], "stats") == 0)
    {
        wifi_8711_at_xfer_stats_t st;
        wifi_8711_at_xfer_get_stats(&st);
        shell_print(sh, "acks ok     : %u", (unsigned)st.acks_ok);
        shell_print(sh, "acks fail   : %u", (unsigned)st.acks_fail);
        shell_print(sh, "stops       : %u", (unsigned)st.stops);
        shell_print(sh, "confirmed   : %u  (8711 said OK)",
                    (unsigned)st.confirmed);
        shell_print(sh, "errors      : %u  (refused, timed out, unrecognised)",
                    (unsigned)st.errors);
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0)
    {
        rc = wifi_8711_at_xfer_stop(NULL, NULL);
        if (rc != 0)
        {
            shell_error(sh, "stop = %d", rc);
            return rc;
        }
        shell_print(sh, "AT+XFERSTOP staged; outcome goes to the log");
        return 0;
    }

    if (strcmp(argv[1], "ack") == 0)
    {
        /* status is mandatory: 0 and 1 are both meaningful, so there is no safe
         * default to fall back on. */
        if (argc < 3 || argc > 4)
        {
            shell_error(sh, "usage: wifi8711 xferctl ack <status> [reason]"
                        "   status 0=fail 1=success");
            return -EINVAL;
        }

        unsigned long status = strtoul(argv[2], NULL, 0);
        unsigned long reason = (argc == 4) ? strtoul(argv[3], NULL, 0) : 0UL;

        if (status > 1UL || reason > 255UL)
        {
            shell_error(sh, "status must be 0 or 1, reason 0..255");
            return -EINVAL;
        }

        rc = wifi_8711_at_xfer_ack((uint8_t)status, (uint8_t)reason,
                                   NULL, NULL);
        if (rc != 0)
        {
            shell_error(sh, "ack = %d%s", rc,
                        (rc == -EINVAL) ? " (success needs reason 0)" : "");
            return rc;
        }
        shell_print(sh, "AT+XFERACK=%u,%u staged; outcome goes to the log",
                    (unsigned)status, (unsigned)reason);
        return 0;
    }

    shell_error(sh, "unknown sub-command '%s'", argv[1]);
    return -EINVAL;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wifi8711,
                               SHELL_CMD(init,    NULL, "probe DT resources, configure slave pads, arm W2B irq",
                                         cmd_init),
                               SHELL_CMD(info,    NULL, "show resolved wiring, line levels and W2B edge counters",
                                         cmd_info),
                               SHELL_CMD(b2w,     NULL, "raw drive of the B2W/READY line (pad check only): b2w <0|1>",
                                         cmd_b2w),
                               SHELL_CMD(xfer,    NULL, "start the slot transport (thread + first slot armed)",
                                         cmd_xfer),
                               SHELL_CMD(pattern, NULL, "stage a recognisable TX slot: pattern [seed]",
                                         cmd_pattern),
                               SHELL_CMD(idle,    NULL, "stage an all-zero TX slot", cmd_idle),
                               SHELL_CMD(at,      NULL, "stage an ATMC command: at <state|startap>",
                                         cmd_at),
                               SHELL_CMD(apinfo,  NULL, "query AP info + auto-start transport + log the reply",
                                         cmd_apinfo),
                               SHELL_CMD(apstop,  NULL, "switch the SoftAP off (AT+WLSTOPAP); ERROR = it was already down",
                                         cmd_apstop),
                               SHELL_CMD(xferctl, NULL,
                                         "transfer control (v2.2 sec.10): xferctl <ack <status> [reason] | stop | stats>",
                                         cmd_xfer_ctrl),
                               SHELL_CMD(wait,    NULL, "block until a slot arrives: wait [ms]",
                                         cmd_wait),
                               SHELL_CMD(stats,   NULL, "transport counters -- which half is broken",
                                         cmd_stats),
                               SHELL_CMD(reset,   NULL, "clear the transport counters", cmd_reset),
                               SHELL_CMD(rx,      NULL, "decode + hexdump the last received slot: rx [bytes]",
                                         cmd_rx),
                               SHELL_CMD(rxdump,  NULL, "hexdump every inbound slot as it arrives: rxdump <on|off>",
                                         cmd_rxdump),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(wifi8711, &sub_wifi8711, "External 8711FA Wi-Fi (SPI slave)", NULL);

#endif /* CONFIG_WIFI_8711 && CONFIG_WIFI_8711_TEST */
