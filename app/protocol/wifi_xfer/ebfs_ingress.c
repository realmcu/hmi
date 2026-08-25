/**
 * @file    ebfs_ingress.c
 * @brief   EBFS slot validation, cross-plane identity check, and hand-off.
 *
 * ---------------------------------------------------------------------------
 * WHAT ARRIVES HERE  --  a byte stream, not a file
 * ---------------------------------------------------------------------------
 * The phone uploads to the 8711 on TCP port 9000 as `EBXF 40B header + body`
 * (eBadge spec §5.2).  The 8711 does not buffer and does not parse: it chops the
 * TCP stream VERBATIM into the payload area of 4096-byte EBFS slots and forwards
 * them on the SPI link.  So:
 *
 *     phone --Wi-Fi/TCP:9000--> 8711FA --SPI EBFS slots--> 8773G (us)
 *
 * and the first 40 bytes of the FIRST slot's payload are the EBXF header, not
 * file content.  That is the one fact this file is built around, and it is a
 * change from the earlier arrangement in which the 8711 parsed EBXF itself and
 * forwarded only the body.  The consequences are worth stating plainly, because
 * each one is a place the old code was right and would now be wrong:
 *
 *   - EBFS `Total Size` and `Offset` measure the STREAM (40 + file bytes), not
 *     the file.  They are still perfectly usable for continuity policing -- see
 *     below -- but they must never be compared against a file length.
 *   - EBFS `File Type`, `Name` and `File CRC32` can only be filled by a peer
 *     that reads EBXF, so they are no longer a trustworthy identity source.
 *     Name Length in particular is now legitimately 0.
 *   - The authoritative data-plane identity is the EBXF header itself, inside
 *     the payload.  xfer_session_on_tcp_data() parses it; that function used to
 *     have no caller and this file is now it.
 *
 * ---------------------------------------------------------------------------
 * WHY THE CONTINUITY ARITHMETIC SURVIVED THE CHANGE UNTOUCHED
 * ---------------------------------------------------------------------------
 * Every comparison below relates Offset, Payload Size and Total Size to each
 * other, and all three are the 8711's own count of the same stream.  So the
 * checks are self-consistent whatever the stream happens to contain -- they
 * police framing, and framing did not change.  Nothing here needs to know where
 * the file starts within the stream, which is exactly why the 40-byte shift can
 * be handled one layer up instead of being smeared across this file.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE IDENTITY CHECK WENT
 * ---------------------------------------------------------------------------
 * §5.2 requires the data plane's size / crc32 / type / name to match the BLE
 * offer, and says a mismatch means close the connection and report 0x16.  That
 * comparison now happens against the EBXF header, on the first slot, inside
 * xfer_session_on_tcp_data() -> xfer_session_check_identity() -- which on
 * failure has already cut the stream (AT+XFERSTOP) and failed the session by the
 * time it returns.  It still costs one slot rather than a whole 2 MiB write
 * followed by a CRC that was never going to match, so nothing was lost by moving
 * it; what WAS lost is the per-slot repetition, and that is the honest outcome
 * of the fields no longer being filled by anyone who read them.
 *
 * ---------------------------------------------------------------------------
 * DATA OUTRUNS THE ASSOCIATION POLL
 * ---------------------------------------------------------------------------
 * A slot can legitimately arrive before this chip knows the phone associated.
 * Nothing on the 8711 reports the join spontaneously, so port_softap polls
 * AT+WLSTATE every 15 s and the round trip is 11..13 s; the phone meanwhile
 * associates and opens the TCP connection at once.  So the session is still in
 * WAIT_STA when the first EBFS slots land, and requiring RECV to route them --
 * which is what this file used to do -- dropped them as "file with no session to
 * route to (state=2)".
 *
 * The fix is to read the START slot as the association evidence it is: bytes
 * cannot arrive from a station that never joined, and they say so sooner and
 * more reliably than the poll.  deliver_on_l2() promotes WAIT_STA -> RECV on a
 * START, and xfer_session_on_sta_joined() is idempotent so the poll's later
 * answer costs nothing.
 *
 * ---------------------------------------------------------------------------
 * VALIDATION SPLIT
 * ---------------------------------------------------------------------------
 * Here, per slot (§6.2): magic, version, Header Size, Payload Size bounds, all
 * offsets inside the slot BEFORE payload or name is touched, the per-chunk CRC32
 * (offset 28 -- NOT offset 12, which is ATMC's, and NOT the whole-file CRC at
 * offset 32), Session ID / Total Size / Chunk Count constant across the stream,
 * START on the first slot with Offset and Chunk Index zero, Offset and Chunk
 * Index strictly consecutive, END with Offset+PayloadSize == TotalSize.
 *
 * Downstream, in xfer_session: the EBXF header, and the whole-file CRC32
 * compared against the BLE offer's value before it commits to flash.  The two
 * CRCs answer different questions -- the per-chunk one catches SPI corruption,
 * the whole-file one catches the phone sending the wrong file -- so both are
 * wanted.
 *
 * ---------------------------------------------------------------------------
 * NO SLOT PAYS FOR THE FLASH WRITE
 * ---------------------------------------------------------------------------
 * §6.3 gives the EBFS END slot no display-completion semantics, so unlike the
 * JPGS path there is no long B2W-low window to hide slow work in.  Every slot's
 * processing back-pressures TCP directly, and the 8711 abandons a slot whose
 * READY does not return within 1000 ms with those bytes already consumed from
 * TCP -- a multi-sector NOR erase does not fit.  So xfer_session buffers the
 * whole file in PSRAM, verifies it, acks, and only then erases and writes,
 * inside the 120 s the 8711 will wait for our AT+XFERACK.
 *
 * §6.2 says the 8773 should abort the session on an illegal slot, and that is
 * what happens: unlike a preview frame, a file has nothing to resynchronise to.
 * A JPGS frame can be dropped and the next START picked up because frames are
 * independent; a file with a hole in it is simply not that file.
 *
 * ---------------------------------------------------------------------------
 * BACK-PRESSURE AND THE HOP TO l2_task
 * ---------------------------------------------------------------------------
 * Identical to jpgs_ingress.c, and for identical reasons -- see the long note in
 * that file's header.  In short: this sink runs on the transport thread with B2W
 * already low, so time spent here is flow control rather than a race (the 8711
 * cannot clock another slot until we return); but session state belongs to
 * l2_task, which preempts us, so the payload is handed over with
 * ebadge_task_post_call() and this thread waits for it to be consumed before
 * letting the slot be re-armed for DMA.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ebfs_ingress.h"
#include "ebxf_frame.h"
#include "xfer_session.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include <zephyr/kernel.h>
#include "wifi_8711.h"
#include "wifi_8711_at.h"
#include "spi_at_protocol.h"

/*----------------------------------------------------------------------------*
 *  EBFS header field offsets (§6.1).  All little-endian.
 *
 *  Spelled out rather than shared with JPGS_OFF_* or SPI_AT_OFF_*: all three
 *  layouts disagree, and they disagree worst in the place that matters most --
 *  the payload CRC32 is at 28 here and in JPGS, but at 12 in ATMC, and offset 32
 *  is the whole-file CRC here and payload in the other two.
 *----------------------------------------------------------------------------*/
#define EBFS_OFF_MAGIC        0U
#define EBFS_OFF_VERSION      4U
#define EBFS_OFF_FLAGS        5U
#define EBFS_OFF_HDR_SIZE     6U
#define EBFS_OFF_SESSION_ID   8U
#define EBFS_OFF_TOTAL_SIZE  12U
#define EBFS_OFF_OFFSET      16U
#define EBFS_OFF_PAYLOAD_SZ  20U
#define EBFS_OFF_CHUNK_IDX   22U
#define EBFS_OFF_CHUNK_CNT   24U
#define EBFS_OFF_FILE_TYPE   26U
#define EBFS_OFF_NAME_LEN    27U
#define EBFS_OFF_CHUNK_CRC   28U
#define EBFS_OFF_FILE_CRC    32U
#define EBFS_OFF_NAME        36U
#define EBFS_OFF_RESERVED    60U

/** Longest usable name: the field is 24 B and §6.1 caps Name Length at 23, so
 *  there is always room for the NUL we append. */
#define EBFS_NAME_MAX        23U

/*----------------------------------------------------------------------------*
 *  Reassembly bookkeeping
 *
 *  No file buffer: payload goes straight to xfer_session, which appends it to
 *  the PSRAM receive cache -- not to flash.  Only what is needed to police §6.2
 *  lives here.
 *----------------------------------------------------------------------------*/
typedef struct
{
    bool     in_file;        /* a START was seen and not yet invalidated     */
    uint32_t session_id;
    uint32_t total_size;
    uint16_t chunk_count;
    uint32_t next_offset;    /* Offset the next slot must carry              */
    uint16_t next_chunk;     /* Chunk Index the next slot must carry         */
} ebfs_asm_t;

static ebfs_asm_t s_asm;

/* Lifetime counters -- they span sessions and keep counting when nothing is
 * listening, which is what makes them useful for "is the link alive at all?". */
static uint32_t s_slots;
static uint32_t s_files_ok;
static uint32_t s_files_dropped;
static uint32_t s_slots_unrouted;

/*----------------------------------------------------------------------------*
 *  Helpers
 *----------------------------------------------------------------------------*/

/** Abandon the file in progress.  There is no resync point for a file, so the
 *  session itself is failed by the caller where appropriate. */
static void file_discard(const char *why)
{
    if (s_asm.in_file)
    {
        s_files_dropped++;
        EBADGE_WARN2("ebfs: file session=%u discarded (%s)",
                     (unsigned)s_asm.session_id, why);
    }
    memset(&s_asm, 0, sizeof(s_asm));
}

/*----------------------------------------------------------------------------*
 *  Hand-off to l2_task
 *
 *  One static descriptor is enough: the transport thread blocks until l2_task
 *  has consumed it, so there is never a second hand-off in flight.
 *----------------------------------------------------------------------------*/
typedef struct
{
    const uint8_t *payload;
    uint16_t       len;
    bool           is_start;   /* in: this is the file's first chunk           */
    bool           accepted;   /* out: false if the session refused the bytes  */
} ebfs_handoff_t;

static ebfs_handoff_t s_handoff;
static K_SEM_DEFINE(ebfs_handoff_done, 0, 1);

/** Runs on l2_task, where touching session state is legal. */
static void deliver_on_l2(void *arg)
{
    ebfs_handoff_t *h = (ebfs_handoff_t *)arg;

    /* Data outruns the association poll, so promote the session here.
     *
     * WAIT_STA -> RECV is otherwise driven only by port_softap's AT+WLSTATE
     * poll seeing clients>0, and that poll runs every 15 s with an 11..13 s
     * round trip.  The phone does not wait for any of it: it associates and
     * opens the TCP connection immediately, so the first EBFS slots routinely
     * arrive while we are still in WAIT_STA -- which used to drop them as
     * "file with no session to route to".
     *
     * A START slot IS the association evidence the poll was going to fetch, and
     * a stronger one: bytes cannot arrive from a station that never joined.  So
     * take the edge from the data.  Only on START, so a mid-file slot from a
     * session we already abandoned cannot resurrect it, and the call is
     * idempotent so the poll's later answer is harmless. */
    if (h->is_start && xfer_session_state() == XFER_SESSION_WAIT_STA)
    {
        EBADGE_LOG("ebfs: START arrived before the join poll -> entering RECV");
        xfer_session_on_sta_joined();
    }

    /* on_tcp_data(), not on_payload(): the payload is raw TCP, so the 40-byte
     * EBXF header is still in front of the file and only that function knows to
     * strip it.  It also runs the §5.2 cross-check against the BLE offer once
     * the header is complete, and tears the session down on mismatch -- so the
     * check that used to happen here, against EBFS header fields, is not missing
     * but relocated to the one place that now has trustworthy values.
     *
     * Its return code is the verdict, and it has to be: after a successful last
     * chunk the session commits and resets to IDLE, which is indistinguishable
     * from the IDLE a teardown leaves behind.  Inferring acceptance from the
     * state would therefore report every completed upload as a rejection. */
    h->accepted = (xfer_session_on_tcp_data(h->payload, h->len) == 0);

    /* Only now may the transport thread return and let its slot be re-armed. */
    k_sem_give(&ebfs_handoff_done);
}

/** Deadlock guard, not a normal path.  Generous because the work on the other
 *  side includes a NOR page program. */
#define EBFS_HANDOFF_TIMEOUT_MS  2000

/**
 * Hand one chunk to the session and wait.
 *
 * @return true if the session accepted it; false if it was rejected or the
 *         hand-off failed, in either case leaving nothing to continue with.
 */static bool deliver(const uint8_t *payload, uint16_t len, bool is_start)
{
    s_handoff.payload    = payload;
    s_handoff.len        = len;
    s_handoff.is_start   = is_start;
    s_handoff.accepted   = false;

    k_sem_reset(&ebfs_handoff_done);
    if (ebadge_task_post_call(deliver_on_l2, &s_handoff) != 0)
    {
        /* Queue full: this chunk is lost, so the file now has a hole in it and
         * cannot be completed.  Saying so is better than letting the whole-file
         * CRC fail later with no explanation. */
        file_discard("l2 queue full");
        return false;
    }

    if (k_sem_take(&ebfs_handoff_done, K_MSEC(EBFS_HANDOFF_TIMEOUT_MS)) != 0)
    {
        /* Returning now would let the slot be re-armed for DMA while l2_task may
         * still be reading it, so say so loudly: a real fault, not a drop. */
        EBADGE_ERR1("ebfs: l2 handoff timed out after %d ms",
                    EBFS_HANDOFF_TIMEOUT_MS);
        file_discard("handoff timeout");
        return false;
    }

    if (!s_handoff.accepted)
    {
        /* xfer_session has already cut the stream and emitted 0x16.  Just stop
         * tracking the file; nothing more will arrive that we want. */
        memset(&s_asm, 0, sizeof(s_asm));
        return false;
    }
    return true;
}

/**
 * EBFS slot sink.  CONTEXT: transport thread, B2W held low for the whole call,
 * which back-pressures the 8711.  Must not block indefinitely; a flash append is
 * acceptable (and is the point), a wait on another thread is not.
 */
static void on_ebfs_slot(const uint8_t *slot, size_t len)
{
    if (slot == NULL || len < WIFI_8711_FILE_HEADER_SIZE)
    {
        return;
    }

    s_slots++;

    /* ---- header sanity (§6.1/§6.2) ----------------------------------------
     * Every bound is checked before payload or name is touched, which is an
     * explicit requirement rather than good manners: Name Length and Payload
     * Size both come off the wire and both index into the slot. */
    uint8_t  version  = slot[EBFS_OFF_VERSION];
    uint8_t  flags    = slot[EBFS_OFF_FLAGS];
    uint16_t hdr_size = spi_at_get_le16(slot + EBFS_OFF_HDR_SIZE);
    uint32_t sid      = spi_at_get_le32(slot + EBFS_OFF_SESSION_ID);
    uint32_t total    = spi_at_get_le32(slot + EBFS_OFF_TOTAL_SIZE);
    uint32_t offset   = spi_at_get_le32(slot + EBFS_OFF_OFFSET);
    uint16_t psize    = spi_at_get_le16(slot + EBFS_OFF_PAYLOAD_SZ);
    uint16_t cidx     = spi_at_get_le16(slot + EBFS_OFF_CHUNK_IDX);
    uint16_t ccnt     = spi_at_get_le16(slot + EBFS_OFF_CHUNK_CNT);
    uint8_t  nlen     = slot[EBFS_OFF_NAME_LEN];
    uint32_t ccrc     = spi_at_get_le32(slot + EBFS_OFF_CHUNK_CRC);

    if (version != WIFI_8711_FILE_VERSION ||
        hdr_size != WIFI_8711_FILE_HEADER_SIZE)
    {
        file_discard("bad version/hdr_size");
        return;
    }
    if (psize == 0U || psize > WIFI_8711_FILE_PAYLOAD_MAX ||
        (size_t)WIFI_8711_FILE_HEADER_SIZE + psize > len)
    {
        file_discard("bad payload size");
        return;
    }
    /* §6.1 gives Name Length the range 1..23, but that assumed a peer that had
     * parsed EBXF and could copy the name out of it.  The 8711 now forwards the
     * TCP stream without looking at it, so it has no name to put here and 0 is
     * the honest value -- rejecting it would reject every slot.  The upper bound
     * is still enforced: it is a length field off the wire, and cheap to police
     * even though nothing below indexes with it any more. */
    if (nlen > EBFS_NAME_MAX)
    {
        file_discard("bad name length");
        return;
    }
    if (sid == 0U)
    {
        /* §6.1 says the 8711 allocates a non-zero session id per connection. */
        file_discard("zero session id");
        return;
    }

    const uint8_t *payload = slot + WIFI_8711_FILE_HEADER_SIZE;

    /* Per-chunk CRC32 at offset 28, covering payload only -- never the header
     * and never the slot padding.  Checked before any state is updated so a
     * corrupt slot cannot advance the expected offset. */
    uint32_t ccrc_calc = spi_at_crc32(payload, psize);
    if (ccrc != ccrc_calc)
    {
        /* Dump the evidence before discarding.
         *
         * A mismatch here has several quite different causes and the bare
         * "chunk crc" line cannot tell them apart, so print what separates
         * them.  The START slot of the same file passes this check, which
         * already rules out the whole-file explanations -- the algorithm, the
         * CRC field offset, and the payload base offset are all proven right by
         * that slot.  What is left is per-slot, and these are the discriminators:
         *
         *   - Which slot.  Offset / Chunk Index against the expected pair says
         *     whether this is the second slot or the last one, and the last is
         *     the interesting case because it is the only short payload.
         *   - Length.  If the CRC covers a different number of bytes than
         *     Payload Size claims, retrying the CRC over a couple of nearby
         *     lengths is what shows it -- a hit at psize-N names the disagreement
         *     outright, where the mismatch alone only says "wrong".
         *   - Content.  If no length matches, the payload bytes themselves are
         *     wrong (SPI corruption, or a slot whose payload never landed), and
         *     the head/tail dump distinguishes "plausible file data" from
         *     zeroes or a shifted copy of the header.
         *
         * Cost is bounded: this runs once per failure and the failure discards
         * the file, so it cannot repeat per slot. */
        EBADGE_WARN2("ebfs: chunk crc mismatch, got=0x%08x calc=0x%08x",
                     (unsigned)ccrc, (unsigned)ccrc_calc);
        EBADGE_WARN2("ebfs:   slot offset=%u chunk=%u",
                     (unsigned)offset, (unsigned)cidx);
        EBADGE_WARN2("ebfs:   expected offset=%u chunk=%u",
                     (unsigned)s_asm.next_offset, (unsigned)s_asm.next_chunk);
        EBADGE_WARN2("ebfs:   psize=%u total=%u",
                     (unsigned)psize, (unsigned)total);
        EBADGE_WARN2("ebfs:   flags=0x%02x nlen=%u",
                     (unsigned)flags, (unsigned)nlen);

        /* Try the CRC over nearby lengths.  A hit names the exact length the
         * sender used, which is a far more actionable answer than "mismatch". */
        for (uint16_t trial = 1U; trial <= 8U; trial++)
        {
            if (psize > trial &&
                spi_at_crc32(payload, (size_t)(psize - trial)) == ccrc)
            {
                EBADGE_WARN2("ebfs:   >>> crc matches %u bytes, %u short of "
                             "Payload Size", (unsigned)(psize - trial),
                             (unsigned)trial);
                break;
            }
            if ((size_t)WIFI_8711_FILE_HEADER_SIZE + psize + trial <= len &&
                spi_at_crc32(payload, (size_t)(psize + trial)) == ccrc)
            {
                EBADGE_WARN2("ebfs:   >>> crc matches %u bytes, %u past "
                             "Payload Size", (unsigned)(psize + trial),
                             (unsigned)trial);
                break;
            }
        }

        /* Dump the WHOLE slot, header included, not a head and a tail.
         *
         * The head/tail pair that used to be here showed 64 of the 4032 bytes the
         * CRC covers -- 1.6% -- so a corruption anywhere in the middle left both
         * excerpts looking perfectly healthy while the CRC still failed.  That is
         * the wrong shape of evidence for the question being asked: we are trying
         * to find WHERE the bytes stop matching what was sent, and that cannot be
         * answered by two windows chosen in advance.
         *
         * From offset 0, so the 64-byte EBFS header is in the same dump as the
         * payload it describes.  Every field the lines above print in decimal is
         * then also visible as the bytes actually on the wire, which is what
         * separates "the field was wrong" from "we read the field wrong".
         *
         * COST, and why it is acceptable exactly here: this runs on the transport
         * thread with B2W held low, so the 8711 is stalled for the duration --
         * 4096 bytes is 256 rows, ~19 KB of console output, ~75 ms at 2 Mbaud
         * (uart2, board overlay).  That is well past the 1000 ms READY budget's
         * comfort zone but it does not matter: this path has already called
         * file_discard(), so the transfer is over and there is no subsequent slot
         * whose bytes could be lost.  It also cannot repeat per slot for the same
         * reason -- the file is gone after the first failure.
         *
         * ebadge_log_hexdump() rather than EBADGE_LOG_HEX(): the latter caps at 32
         * bytes internally while still printing the length it was ASKED for, so
         * "head len=64" was in fact 32 bytes plus an ellipsis. */
        ebadge_log_hexdump("ebfs bad slot", slot, (uint32_t)len,
                           (uint32_t)len);

        /* Where the CRC range sits INSIDE the dump above, in the dump's own
         * offsets.  Without this the reader has to add 64 to everything by hand,
         * and the one row that matters most -- the last one the CRC covers -- is
         * the easiest to miscount. */
        EBADGE_WARN2("ebfs:   crc covers dump offsets 0x%04x..0x%04x",
                     (unsigned)WIFI_8711_FILE_HEADER_SIZE,
                     (unsigned)(WIFI_8711_FILE_HEADER_SIZE + psize));

        /* Kept as a compact restatement of the two windows that matter most --
         * the CRC's first and last bytes -- so the interesting rows can be found
         * without scrolling 256 lines.  payload+psize-32 is the end of the CRC
         * range, and on a full slot it is also the end of the DMA target buffer,
         * which is where a short DMA write leaves stale bytes behind. */
        EBADGE_LOG_HEX("ebfs   crc range head", payload, 32);
        if (psize > 32U)
        {
            EBADGE_LOG_HEX("ebfs   crc range tail",
                           payload + (psize - 32U), 32);
        }

        file_discard("chunk crc");
        return;
    }

    bool is_start = (flags & WIFI_8711_FILE_FLAG_START) != 0U;
    bool is_end   = (flags & WIFI_8711_FILE_FLAG_END)   != 0U;

    /* ---- START: begin a file (§6.2 first-chunk rules) ------------------- */
    if (is_start)
    {
        if (s_asm.in_file)
        {
            /* A new START while a file is open means the previous upload never
             * finished -- the sender gave up on it, so we do too. */
            file_discard("new START mid-file");
        }
        if (offset != 0U || cidx != 0U)
        {
            EBADGE_WARN2("ebfs: START with offset=%u chunk=%u, ignored",
                         (unsigned)offset, (unsigned)cidx);
            return;
        }
        /* Total Size counts the stream, so a file at exactly the 2 MiB cap
         * legitimately reports 2 MiB + 40 here.  Allowing the header keeps the
         * bound a sanity check on a wire field rather than a second, stricter
         * and differently-shifted copy of the size policy -- the real size limit
         * is enforced by xfer_session against the BLE offer, on the file length
         * proper, where 2 MiB actually means 2 MiB. */
        if (total <= EBXF_HDR_LEN ||
            total > (uint32_t)WIFI_8711_FILE_SIZE_MAX + EBXF_HDR_LEN)
        {
            EBADGE_WARN1("ebfs: START total_size=%u out of range, ignored",
                         (unsigned)total);
            return;
        }

        /* Only the file session can receive these; a preview stream never gets
         * EBFS slots, which is the whole reason EBFS has its own magic.
         *
         * WAIT_STA counts as routable: the phone associates and starts sending
         * without waiting to be noticed, so a START in WAIT_STA is the normal
         * case rather than a stray, and deliver_on_l2() promotes the session on
         * the strength of it.  Requiring RECV here is what produced the "file
         * with no session to route to (state=2)" drop. */
        xfer_session_state_t xst = xfer_session_state();
        if (xst != XFER_SESSION_RECV && xst != XFER_SESSION_WAIT_STA)
        {
            /* Nobody asked for this.  Counted rather than logged per slot: an
             * 8711 left forwarding after a session ended would otherwise bury
             * the log at hundreds of slots per file. */
            s_slots_unrouted++;
            if ((s_slots_unrouted % 64U) == 1U)
            {
                EBADGE_LOG2("ebfs: file with no session to route to "
                            "(state=%d, x%u)", (int)xst,
                            (unsigned)s_slots_unrouted);
            }
            return;
        }

        s_asm.in_file     = true;
        s_asm.session_id  = sid;
        s_asm.total_size  = total;
        s_asm.chunk_count = ccnt;
        s_asm.next_offset = 0U;
        s_asm.next_chunk  = 0U;

        EBADGE_LOG3("ebfs: stream session=%u bytes=%u chunks=%u "
                    "(incl. EBXF hdr) -> store",
                    (unsigned)sid, (unsigned)total, (unsigned)ccnt);
    }
    else if (!s_asm.in_file)
    {
        /* Mid-file slot with no file open: either we gave up on it, or the link
         * came up mid-transfer.  There is no START to wait for that would make
         * this file whole, so it is only counted. */
        s_slots_unrouted++;
        return;
    }

    /* ---- continuation constraints (§6.2) -------------------------------- */
    if (sid != s_asm.session_id || total != s_asm.total_size ||
        ccnt != s_asm.chunk_count)
    {
        file_discard("identity changed mid-file");
        return;
    }
    if (offset != s_asm.next_offset || cidx != s_asm.next_chunk)
    {
        file_discard("out-of-order chunk");
        return;
    }
    if ((uint64_t)offset + psize > s_asm.total_size)
    {
        file_discard("payload overruns total_size");
        return;
    }
    if (is_end && ((uint64_t)offset + psize != s_asm.total_size))
    {
        file_discard("END short of total_size");
        return;
    }
    if (!is_end && ((uint64_t)offset + psize == s_asm.total_size))
    {
        /* Complete but END not set.  Trusting the byte count over the flag would
         * leave the sender and us disagreeing about whether the file is done,
         * and the ack we owe depends on knowing. */
        file_discard("complete without END flag");
        return;
    }

    /* ---- forward, then advance ------------------------------------------ */
    if (!deliver(payload, psize, is_start))
    {
        /* Rejected or lost; deliver() has already cleared the bookkeeping and,
         * where relevant, xfer_session has failed the session. */
        return;
    }

    s_asm.next_offset += psize;
    s_asm.next_chunk++;

    if (is_end)
    {
        s_files_ok++;
        EBADGE_LOG2("ebfs: stream session=%u complete (%u bytes incl. hdr)",
                    (unsigned)s_asm.session_id, (unsigned)s_asm.next_offset);
        /* xfer_session saw the last byte inside deliver() above and has already
         * verified, committed and sent the ack.  Nothing left but to forget. */
        memset(&s_asm, 0, sizeof(s_asm));
    }
}

void ebfs_ingress_init(void)
{
    memset(&s_asm, 0, sizeof(s_asm));
    wifi_8711_at_set_file_sink(on_ebfs_slot);
    EBADGE_LOG("ebfs: sink registered (file uploads -> xfer_session)");
}

void ebfs_ingress_reset(void)
{
    if (s_asm.in_file)
    {
        EBADGE_LOG1("ebfs: reset, dropping file session=%u",
                    (unsigned)s_asm.session_id);
    }
    memset(&s_asm, 0, sizeof(s_asm));
}

uint32_t ebfs_ingress_slots(void)         { return s_slots; }
uint32_t ebfs_ingress_files_ok(void)      { return s_files_ok; }
uint32_t ebfs_ingress_files_dropped(void) { return s_files_dropped; }

#else /* !CONFIG_WIFI_8711 */

void ebfs_ingress_init(void)
{
    /* No SPI link to the 8711 on this build, so no upload can arrive.  Silent
     * rather than a warning: this is a build configuration, not a fault. */
}

void ebfs_ingress_reset(void)             { }
uint32_t ebfs_ingress_slots(void)         { return 0U; }
uint32_t ebfs_ingress_files_ok(void)      { return 0U; }
uint32_t ebfs_ingress_files_dropped(void) { return 0U; }

#endif /* CONFIG_WIFI_8711 */
