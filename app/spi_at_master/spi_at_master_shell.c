/**
 * @file    spi_at_master_shell.c
 * @brief   `atm` shell group -- the user interface of the AT-over-SPI master.
 *
 * ---------------------------------------------------------------------------
 * TWO LAYERS, AND THE COMMANDS ARE SPLIT ALONG THAT SEAM
 * ---------------------------------------------------------------------------
 * This started as a bring-up harness over the bare transport, when the question
 * was "does a typed line reach the 8711 at all?".  There is a real client stack
 * on top of that transport now, and the two answer different questions:
 *
 *   the TRANSPORT (spi_at_master)  slots, sync lines, clock, hexdumps.  Answers
 *                                  "is the wiring right?"
 *   the CLIENT (at_client)         one command's lifecycle: queued, sent, OK /
 *                                  ERROR: n / timeout.  Answers "did the 8711
 *                                  understand it?"
 *
 * `atm send` goes through the CLIENT, and that is the important change.  It used
 * to call spi_at_master_send_text() and then block on the next inbound slot, which
 * is wrong in two ways now that anything else can be talking to the 8711:
 *
 *   - it bypassed the single-flight queue, so a typed line could interleave with a
 *     transfer's AT+SKTSENDRAW.  The 8711 matches responses by "it is the only one
 *     outstanding" and drops a 4 KiB ring overflow silently, so the failure is
 *     lost commands with nothing in any log.
 *   - "the next slot" is not "my reply".  A [$][SKT][DATA] event or another
 *     command's OK satisfies the wait, so it reported success for a command that
 *     had not been answered.
 *
 * Going through at_client fixes both: the line is queued behind whatever is in
 * flight, and the completion callback is genuinely THIS command's outcome.
 *
 * `atm raw` exists for the one command a text-line interface cannot express --
 * AT+SKTSENDRAW's transparent-transmission dance -- and is the cheap way to find
 * out whether Host Control Mode is enabled on the 8711 before a real transfer
 * discovers it (an EBXR is the first raw send the firmware ever does).
 *
 * Nothing here can be self-tested.  Even as master, where we do generate the
 * clock, no transfer starts until the 8711 raises S2M -- so every command below
 * needs a live, AT-capable peer.  A test that passed without one would be worse
 * than no test.
 */
#if defined(CONFIG_SPI_AT_MASTER) && defined(CONFIG_SPI_AT_MASTER_SHELL)

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "spi_at_master.h"
#include "at_client.h"
#include "at_socket.h"

/* How long `atm send` waits for the command to COMPLETE -- not for a slot to
 * arrive.  Deliberately longer than AT_CLIENT_TIMEOUT_MS (3 s): at_client's own
 * budget starts when the line goes on the wire, and this one also has to cover
 * however long the command sat in the queue behind something else.  A
 * WLSTARTAP in front of it can hold the link for ~25 s. */
#define SEND_REPLY_WAIT_MS      30000U

/* Longest payload `atm raw` accepts, in bytes.  at_client's own cap; named here
 * so the usage message and the check cannot disagree. */
#define RAW_MAX                 AT_CLIENT_RAW_MAX

/*----------------------------------------------------------------------------*
 *  Waiting for a command from the shell thread
 *
 *  The shell thread MAY block, which makes this the one place in the firmware
 *  where an AT command can be awaited synchronously -- everything else is a
 *  callback because l2_task and the work queue must not sleep.  Worth doing here:
 *  a person typing a command wants its answer under the command they typed, not
 *  three log lines later interleaved with a transfer.
 *
 *  One command at a time, enforced by the semaphore's initial count of 1.  Two
 *  concurrent shell sessions would otherwise share s_wait and each collect the
 *  other's result.
 *----------------------------------------------------------------------------*/
K_SEM_DEFINE(s_shell_slot, 1, 1);       /* only one `atm send` in progress     */
K_SEM_DEFINE(s_shell_done, 0, 1);       /* signalled by shell_done()          */

static at_result_t s_res;
static int         s_error_no;

static void shell_done(at_result_t res, int error_no, void *user)
{
    ARG_UNUSED(user);

    s_res      = res;
    s_error_no = error_no;
    k_sem_give(&s_shell_done);
}

/** Print a completed command's outcome.  @return the shell's exit code. */
static int report(const struct shell *sh)
{
    switch (s_res)
    {
    case AT_RES_OK:
        shell_print(sh, "OK");
        return 0;

    case AT_RES_ERROR:
        /* The number is the whole diagnosis: "command does not exist" and
         * "socket refused" arrive by the same route and only this separates
         * them.  4 in particular is what an AT+SKTSENDRAW answers when Host
         * Control Mode is off. */
        shell_error(sh, "ERROR: %d", s_error_no);
        return -EIO;

    case AT_RES_TIMEOUT:
        shell_warn(sh, "timeout -- nothing terminated it.  `atm stats` shows "
                   "whether the transport moved anything at all");
        return -ETIMEDOUT;

    case AT_RES_RESET:
        shell_warn(sh, "the 8711 rebooted under us (ATCMD READY) -- everything "
                   "in flight was lost, including this");
        return -ECONNRESET;

    case AT_RES_SEND_FAIL:
        shell_error(sh, "could not get the line onto the wire -- no S2M?");
        return -EIO;

    default:
        shell_error(sh, "unknown result %d", (int)s_res);
        return -EIO;
    }
}

/** Map a submit refusal onto a message that names the cause. */
static int report_submit(const struct shell *sh, int rc)
{
    switch (rc)
    {
    case -ENODEV:
        shell_error(sh, "at_client is not initialised -- `atm init` first");
        break;
    case -EMSGSIZE:
        shell_error(sh, "command too long (max %u chars)",
                    (unsigned)(AT_CLIENT_CMD_MAX - 1U));
        break;
    case -ENOSPC:
        shell_error(sh, "queue full (%u deep) -- something is mid-transfer",
                    (unsigned)AT_CLIENT_QUEUE_DEPTH);
        break;
    default:
        shell_error(sh, "submit failed %d", rc);
        break;
    }
    return rc;
}

/*----------------------------------------------------------------------------*
 *  Commands
 *----------------------------------------------------------------------------*/

static int cmd_init(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    /* at_client_init() calls spi_at_master_init() itself and takes over its RX
     * callback, so this must be the client rather than the transport: bringing
     * the transport up alone would leave the parser uninstalled and every
     * inbound line unread. */
    int rc = at_client_init();

    shell_print(sh, "at_client_init() = %d (%s)", rc,
                (rc == 0) ? "ok" : "failed");
    if (rc == 0)
    {
        shell_print(sh, "link ready  : %s  (\"ATCMD READY\" seen)",
                    at_client_link_ready() ? "yes" : "not yet");
    }
    return rc;
}

/* Dumps what the overlay actually produced.  The fastest way to catch the one
 * mistake this module is most exposed to -- a sync line copied from the slave
 * snippet, which compiles perfectly and drives the wrong pad the wrong way. */
static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    bool tx_dump = false, rx_dump = false;

    spi_at_master_get_dump(&tx_dump, &rx_dump);

    shell_print(sh, "role      : MASTER (we drive SCLK/CS)");
    shell_print(sh, "ready     : %s", spi_at_master_ready() ? "yes" : "no");
    shell_print(sh, "clock     : %u Hz, mode%u%u, 8-bit MSB",
                (unsigned)SPI_AT_SCLK_HZ,
                (unsigned)SPI_AT_SPI_MODE_CPOL, (unsigned)SPI_AT_SPI_MODE_CPHA);
    shell_print(sh, "slot      : %u B fixed both ways, payload max %u",
                (unsigned)SPI_AT_SLOT_SIZE, (unsigned)SPI_AT_PAYLOAD_MAX);
    /* Logical levels, not raw pad levels: 1 means asserted on both lines even
     * though M2S is active-low in hardware.  Printed with the physical meaning
     * spelled out so the two cannot be confused. */
    shell_print(sh, "m2s (out) : %d  (1 = asserted = pad LOW = 'prepare to recv')",
                spi_at_master_get_m2s());
    shell_print(sh, "s2m (in)  : %d  (1 = asserted = pad HIGH = 8711 ready)",
                spi_at_master_get_s2m());
    /* One threshold, both directions, and it is a threshold not a cap: a payload
     * at or under it prints whole, one over it prints its length only.  Spelled
     * out here because "max" on its own reads like truncation. */
    shell_print(sh, "dump      : tx=%s  rx=%s  (whole if <=%u B, length only if over)",
                tx_dump ? "on" : "off", rx_dump ? "on" : "off",
                (unsigned)SPI_AT_DUMP_MAX);
    return 0;
}

/* Forward a typed AT command line to the 8711 and wait for its outcome.
 *
 * Rejoins argv with single spaces rather than requiring quotes, because the
 * commands being typed here look like `AT+WLCONN ssid=x pw=y` and demanding
 * quotes around that is a trap nobody remembers on the second day.  The cost is
 * that runs of whitespace collapse, which no AT command cares about.
 *
 * The response BODY is not printed here.  at_client logs every inbound line as
 * "at< <line>" already (at_client_set_log_lines(), on by default), so a sink here
 * would print each one twice -- and the log has the timestamps. */
static int cmd_send(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_error(sh, "usage: atm send <AT command>");
        return -EINVAL;
    }

    char    line[AT_CLIENT_CMD_MAX];
    size_t  used = 0;

    for (size_t i = 1; i < argc; i++)
    {
        size_t n = strlen(argv[i]);

        /* +1 for the joining space, +1 for the NUL.  No room reserved for CRLF:
         * at_client appends that itself, after the length check. */
        if (used + n + 2U > sizeof(line))
        {
            shell_error(sh, "command too long (max %u chars)",
                        (unsigned)(sizeof(line) - 2U));
            return -EMSGSIZE;
        }
        if (used > 0)
        {
            line[used++] = ' ';
        }
        memcpy(&line[used], argv[i], n);
        used += n;
    }
    line[used] = '\0';

    /* Refuse rather than queue behind another shell command: the two would share
     * s_res and each print the other's outcome. */
    if (k_sem_take(&s_shell_slot, K_NO_WAIT) != 0)
    {
        shell_error(sh, "another `atm send` is still waiting");
        return -EBUSY;
    }

    k_sem_reset(&s_shell_done);

    int rc = at_client_submit(line, NULL, shell_done, NULL, 0U);

    if (rc != 0)
    {
        k_sem_give(&s_shell_slot);
        return report_submit(sh, rc);
    }

    shell_print(sh, "queued \"%s\"", line);

    if (k_sem_take(&s_shell_done, K_MSEC(SEND_REPLY_WAIT_MS)) != 0)
    {
        /* at_client guarantees exactly one completion per submission, so this is
         * not "the command failed" -- it is the command still being queued after
         * SEND_REPLY_WAIT_MS.  The slot is deliberately NOT returned: shell_done()
         * is still armed and would write s_res out from under the next command.
         * One stuck `atm send` costs the shell that command until reboot, which is
         * the right trade against silently crossed results. */
        shell_warn(sh, "no completion within %u ms -- the command is still "
                   "queued; `atm at` shows the client's counters",
                   (unsigned)SEND_REPLY_WAIT_MS);
        return -ETIMEDOUT;
    }

    int ret = report(sh);

    k_sem_give(&s_shell_slot);
    return ret;
}

/* Put raw bytes on a socket -- the transparent-transmission dance, which no
 * amount of typing at `atm send` can reach.
 *
 * The reason to have it: AT+SKTSENDRAW requires Host Control Mode on the 8711 and
 * answers "ERROR: 4" without it.  The firmware's first raw send is the EBXR at the
 * END of a file transfer, so discovering the missing menuconfig option there costs
 * a whole transfer to find out.  One `atm raw` during bring-up answers it. */
static int cmd_raw(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3)
    {
        shell_error(sh, "usage: atm raw <link> <hex>   e.g. atm raw 1 45425852");
        shell_print(sh, "  <hex> is an even number of hex digits, max %u bytes",
                    (unsigned)RAW_MAX);
        return -EINVAL;
    }

    unsigned long link = strtoul(argv[1], NULL, 0);

    if (link >= AT_SOCKET_LINK_MAX)
    {
        shell_error(sh, "link %lu out of range [0,%u)", link,
                    (unsigned)AT_SOCKET_LINK_MAX);
        return -EINVAL;
    }

    const char *hex = argv[2];
    size_t      hn  = strlen(hex);

    if (hn == 0U || (hn & 1U) != 0U || (hn / 2U) > RAW_MAX)
    {
        shell_error(sh, "hex must be an even count of digits, 2..%u of them",
                    (unsigned)(RAW_MAX * 2U));
        return -EINVAL;
    }

    uint8_t buf[RAW_MAX];
    size_t  n = hn / 2U;

    for (size_t i = 0; i < n; i++)
    {
        char   pair[3] = { hex[i * 2U], hex[(i * 2U) + 1U], '\0' };
        char  *end     = NULL;
        long   v       = strtol(pair, &end, 16);

        if (end != &pair[2])
        {
            shell_error(sh, "\"%s\" is not hex (at byte %u)", pair, (unsigned)i);
            return -EINVAL;
        }
        buf[i] = (uint8_t)v;
    }

    if (k_sem_take(&s_shell_slot, K_NO_WAIT) != 0)
    {
        shell_error(sh, "another `atm send` is still waiting");
        return -EBUSY;
    }

    k_sem_reset(&s_shell_done);

    int rc = at_client_send_raw((uint8_t)link, buf, (uint16_t)n,
                                shell_done, NULL);

    if (rc != 0)
    {
        k_sem_give(&s_shell_slot);
        return report_submit(sh, rc);
    }

    shell_print(sh, "queued %u raw bytes for link %lu", (unsigned)n, link);

    /* AT_CLIENT_TT_TIMEOUT_MS bounds the dance once it starts, but the queue in
     * front of it is unbounded in time, so the same generous wait applies. */
    if (k_sem_take(&s_shell_done, K_MSEC(SEND_REPLY_WAIT_MS)) != 0)
    {
        shell_warn(sh, "no completion within %u ms", (unsigned)SEND_REPLY_WAIT_MS);
        return -ETIMEDOUT;
    }

    if (s_res == AT_RES_ERROR && s_error_no == 4)
    {
        /* Worth naming rather than leaving as a number: this is the one failure
         * that is a build option on the OTHER chip, so nothing on this side can
         * be changed to fix it. */
        shell_error(sh, "ERROR: 4 -- Host Control Mode is probably disabled on "
                    "the 8711.  AT+SKTSENDRAW needs it, and the EBXR ack is "
                    "the firmware's only user of it.");
        k_sem_give(&s_shell_slot);
        return -EIO;
    }

    int ret = report(sh);

    k_sem_give(&s_shell_slot);
    return ret;
}

/* Deliberately no `atm m2s <0|1>`.  The slave build had one because B2W was a
 * standing "I am armed" level that could be inspected on its own; M2S is a
 * per-transaction request the transport owns end to end, and poking it with
 * nothing staged just answers the 8711 with a dummy slot.  `atm send` plus a
 * scope on P2_7 gives the same pad check without lying to the peer. */

static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    spi_at_master_stats_t st;

    spi_at_master_get_stats(&st);

    shell_print(sh, "-- transport (slots and sync lines) --");
    shell_print(sh, "s2m_edges    %u   (0 = the 8711 never answers at all)",
                (unsigned)st.s2m_edges);
    shell_print(sh, "slots        %u", (unsigned)st.slots);
    shell_print(sh, "tx_frames    %u", (unsigned)st.tx_frames);
    shell_print(sh, "rx_frames    %u", (unsigned)st.rx_frames);
    shell_print(sh, "rx_empty     %u   (valid header, no payload -- normal)",
                (unsigned)st.rx_empty);
    shell_print(sh, "rx_bad_magic %u   (nonzero + all 00/FF = mode or clock)",
                (unsigned)st.rx_bad_magic);
    shell_print(sh, "rx_bad_len   %u", (unsigned)st.rx_bad_len);
    shell_print(sh, "xfer_err     %u", (unsigned)st.xfer_err);
    shell_print(sh, "ready_timeout %u  (requests the 8711 never answered)",
                (unsigned)st.ready_timeout);
    shell_print(sh, "last_result  %d", (int)st.last_result);
    return 0;
}

/* The client's own view, which is a different question from the transport's.
 *
 * Reading the two together is the whole diagnostic: slots moving with no lines
 * emitted means the bytes are arriving unparseable (clock or mode), while lines
 * emitted with every command timing out means they are arriving fine and the 8711
 * is not answering what we asked. */
static int cmd_at(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    at_client_stats_t  cs;
    at_parser_stats_t  ps;
    at_socket_stats_t  ss;

    at_client_get_stats(&cs);
    at_client_get_parser_stats(&ps);
    at_socket_get_stats(&ss);

    shell_print(sh, "link ready   : %s     busy: %s",
                at_client_link_ready() ? "yes" : "no",
                at_client_busy() ? "a command is on the wire" : "idle");
    shell_print(sh, "line logging : %s",
                at_client_get_log_lines() ? "on" : "off");

    shell_print(sh, "-- client (one command's lifecycle) --");
    shell_print(sh, "submitted    %u", (unsigned)cs.submitted);
    shell_print(sh, "ok / error   %u / %u",
                (unsigned)cs.completed_ok, (unsigned)cs.completed_err);
    shell_print(sh, "timeouts     %u", (unsigned)cs.timeouts);
    shell_print(sh, "resets       %u   (\"ATCMD READY\" -- the 8711 rebooted)",
                (unsigned)cs.resets);
    shell_print(sh, "send_fails   %u   (never reached the wire)",
                (unsigned)cs.send_fails);
    shell_print(sh, "queue_full   %u   (submissions refused, -ENOSPC)",
                (unsigned)cs.queue_full);
    shell_print(sh, "events       %u   ([$] indications fanned out)",
                (unsigned)cs.events);
    shell_print(sh, "orphan_lines %u   (body text with nothing in flight)",
                (unsigned)cs.orphan_lines);

    shell_print(sh, "-- parser (bytes -> lines) --");
    shell_print(sh, "lines        %u", (unsigned)ps.lines);
    shell_print(sh, "data_events  %u   %u bytes",
                (unsigned)ps.data_events, (unsigned)ps.data_bytes);
    shell_print(sh, "line_ovf     %u", (unsigned)ps.line_ovf);
    shell_print(sh, "bad_heads    %u   (looked like a DATA head, did not parse)",
                (unsigned)ps.bad_heads);
    /* Called out because everything after one is suspect: a desync means the
     * payload did not end where <len> said, so the parser resumed mid-stream. */
    shell_print(sh, "desyncs      %u   %s", (unsigned)ps.desyncs,
                (ps.desyncs != 0U) ? "<-- everything after one is suspect" : "");

    shell_print(sh, "-- sockets --");
    shell_print(sh, "servers      %u opened", (unsigned)ss.servers_opened);
    shell_print(sh, "seeds        %u connected, %u closed",
                (unsigned)ss.seeds_connected, (unsigned)ss.seeds_closed);
    shell_print(sh, "data         %u events, %u bytes",
                (unsigned)ss.data_events, (unsigned)ss.data_bytes);
    shell_print(sh, "unrouted     %u   (DATA naming a link we do not know)",
                (unsigned)ss.data_unrouted);
    shell_print(sh, "unparsed     %u   ([$][SKT][EVENT] we could not read)",
                (unsigned)ss.events_unparsed);
    return 0;
}

/* The link table -- which servers exist and what is connected to them.
 *
 * The one thing worth checking by hand, because AT+SKTSERVER answers a bare "OK"
 * without echoing the link id it used.  Our table is therefore the ONLY record of
 * which link is the file port and which is the preview port, and a disagreement
 * with the 8711 routes one session's bytes into the other. */
static int cmd_links(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    unsigned shown = 0;

    for (uint8_t i = 0; i < AT_SOCKET_LINK_MAX; i++)
    {
        at_socket_link_t l;

        if (!at_socket_link_get(i, &l) || l.role == AT_LINK_FREE)
        {
            continue;
        }

        if (l.role == AT_LINK_SERVER)
        {
            shell_print(sh, "link %2u  SERVER  port %u   %u connection(s)",
                        (unsigned)i, (unsigned)l.port,
                        (unsigned)at_socket_seed_count(i));
        }
        else
        {
            shell_print(sh, "link %2u  SEED    port %u   accepted by link %u",
                        (unsigned)i, (unsigned)l.port,
                        (unsigned)l.server_link);
        }
        shown++;
    }

    if (shown == 0U)
    {
        shell_print(sh, "no links.  Nothing has called AT+SKTSERVER yet -- the "
                    "listeners are created when a phone connects over BLE.");
    }
    return 0;
}

/* Silence the "at< <line>" log without a reflash.
 *
 * It prints on the transport thread with M2S asserted, so under a burst of socket
 * events the logging itself is what throttles the 8711.  Same reason `atm dump` is
 * runtime rather than an #ifdef. */
static int cmd_lines(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2)
    {
        shell_error(sh, "usage: atm lines <on|off>");
        return -EINVAL;
    }

    if (strcmp(argv[1], "on") == 0)
    {
        at_client_set_log_lines(true);
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        at_client_set_log_lines(false);
    }
    else
    {
        shell_error(sh, "expected on or off, got \"%s\"", argv[1]);
        return -EINVAL;
    }

    shell_print(sh, "line logging %s",
                at_client_get_log_lines() ? "on" : "off");
    return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    spi_at_master_reset_stats();
    at_client_reset_stats();
    shell_print(sh, "counters cleared (transport and client)");
    return 0;
}

/* The dump runs on the transport thread with M2S still asserted, so leaving it
 * on under a burst throttles the peer.  Being able to stop it without a reflash
 * is the reason this is runtime and not an #ifdef. */
static int cmd_dump(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        shell_error(sh, "usage: atm dump <on|off> [tx|rx]");
        return -EINVAL;
    }

    bool on;

    if (strcmp(argv[1], "on") == 0)       { on = true;  }
    else if (strcmp(argv[1], "off") == 0) { on = false; }
    else
    {
        shell_error(sh, "expected on or off, got \"%s\"", argv[1]);
        return -EINVAL;
    }

    bool tx_dump, rx_dump;

    spi_at_master_get_dump(&tx_dump, &rx_dump);

    if (argc == 2)
    {
        tx_dump = on;
        rx_dump = on;
    }
    else if (strcmp(argv[2], "tx") == 0)
    {
        tx_dump = on;
    }
    else if (strcmp(argv[2], "rx") == 0)
    {
        rx_dump = on;
    }
    else
    {
        shell_error(sh, "expected tx or rx, got \"%s\"", argv[2]);
        return -EINVAL;
    }

    spi_at_master_set_dump(tx_dump, rx_dump);
    shell_print(sh, "dump tx=%s rx=%s", tx_dump ? "on" : "off",
                rx_dump ? "on" : "off");
    return 0;
}

/* Wait for an unsolicited slot -- the 8711 raising S2M on its own.
 *
 * A TRANSPORT-level command, and the only one still worth having at that level:
 * it watches for raw slot activity with no command outstanding, which is how you
 * tell "the 8711 is silent" from "the 8711 is talking and we cannot parse it".
 * For anything with a command attached, use `atm send`. */
static int cmd_wait(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t ms = 3000U;

    if (argc == 2)
    {
        ms = (uint32_t)strtoul(argv[1], NULL, 0);
    }
    else if (argc > 2)
    {
        shell_error(sh, "usage: atm wait [ms]");
        return -EINVAL;
    }

    int rc = spi_at_master_wait_rx(ms);

    if (rc != 0)
    {
        shell_warn(sh, "nothing within %u ms", (unsigned)ms);
        return rc;
    }
    shell_print(sh, "slot received (see hexdump above)");
    return 0;
}

/*----------------------------------------------------------------------------*
 *  Registration
 *----------------------------------------------------------------------------*/
SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_atm,
    SHELL_CMD(init,  NULL, "Bring up transport + parser + client",               cmd_init),
    SHELL_CMD(info,  NULL, "Role, clock, slot geometry and both line levels",    cmd_info),
    SHELL_CMD(send,  NULL, "atm send <AT cmd> -- queue a line, await its OK",    cmd_send),
    SHELL_CMD(raw,   NULL, "atm raw <link> <hex> -- AT+SKTSENDRAW payload",      cmd_raw),
    SHELL_CMD(at,    NULL, "Client, parser and socket counters",                 cmd_at),
    SHELL_CMD(links, NULL, "The socket link table (which link is which port)",   cmd_links),
    SHELL_CMD(lines, NULL, "atm lines <on|off> -- log every inbound AT line",    cmd_lines),
    SHELL_CMD(wait,  NULL, "atm wait [ms] -- block for an unsolicited slot",     cmd_wait),
    SHELL_CMD(dump,  NULL, "atm dump <on|off> [tx|rx] -- hexdump control",       cmd_dump),
    SHELL_CMD(stats, NULL, "Transport counters, split by failure mode",          cmd_stats),
    SHELL_CMD(reset, NULL, "Clear the counters",                                 cmd_reset),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(atm, &sub_atm, "AT-over-SPI master (8711)", NULL);

#endif /* CONFIG_SPI_AT_MASTER && CONFIG_SPI_AT_MASTER_SHELL */
