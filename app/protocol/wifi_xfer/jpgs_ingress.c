/**
 * @file    jpgs_ingress.c
 * @brief   Where Wi-Fi payload bytes enter this chip, get reassembled from JPGS
 *          slots, verified, and routed to whichever session asked for them.
 *
 * ---------------------------------------------------------------------------
 * THE ONE THING TO UNDERSTAND: THERE IS NO TCP SOCKET ON THIS CHIP
 * ---------------------------------------------------------------------------
 * stream_session / xfer_session are written against ebadge_port_tcp.h, as if
 * this device terminated the phone's TCP connection.  It does not.  The real
 * topology is (phone-to-8711-jpeg-tcp-protocol.md sec.1):
 *
 *     phone --Wi-Fi/TCP:5004--> 8711FA --SPI JPGS slots--> 8773G (us)
 *
 * The 8711 is the SoftAP *and* the TCP server.  It accepts the connection,
 * parses the phone's "JPG <size> <seq>" framing itself, and forwards the
 * payload to us as JPGS slots on the SPI link.  So the bytes arrive here
 * already stripped of TCP and of the phone-side framing -- port_tcp's on_data
 * will never fire from a socket, because there is no socket.  This file is that
 * on_data: it calls the session entry points directly, which is why it holds
 * the only two callers of xfer_session_on_payload() /
 * stream_session_on_frame_chunk() outside port_tcp.
 *
 * ---------------------------------------------------------------------------
 * THE SECOND THING: THERE IS NO EBXF/EBXS HEADER ON THIS PATH EITHER
 * ---------------------------------------------------------------------------
 * §5.2 / §6.2 of the eBadge spec put a 40-byte EBXF header in front of a file
 * and a 14-byte EBXS header in front of every preview frame.  Neither can reach
 * this chip *on this path*.  The 8711's preview server (firmware example
 * tcp_jpg_spi_forwarder, port 5004) validates that every payload *is a JPEG* --
 * it checks the first two bytes are FF D8 and the last two FF D9, and answers
 * `ERR <seq> JPEG` otherwise (phone-to-8711-jpeg-tcp-protocol.md sec.4
 * constraints and sec.6.2).  A payload prefixed with "EBXF" fails that check at
 * the TCP door and is never forwarded, so a session waiting for those 40 bytes
 * would parse FF D8.. as a bad magic and fail every transfer.
 *
 * That is why the two entry points used here are the *headerless* ones:
 *
 *   xfer_session_on_payload()        pure file bytes; identity (name, type,
 *                                    size, crc32) comes from the BLE 0x10
 *                                    offer, which EBXF only ever restated
 *   stream_session_on_frame_chunk()  frame bytes plus the geometry EBXS would
 *                                    have carried -- taken from the JPGS
 *                                    header's TotalSize / Offset / END instead
 *
 * The EBXF/EBXS parsers are deliberately kept (xfer_session_on_tcp_data() /
 * stream_session_on_tcp_data()) because they are correct for the topology the
 * spec assumes, and a firmware that terminates TCP on this chip would need them
 * unchanged.  They simply have no caller today.
 *
 * SPI v2.2 note: the EBXF header does now reach the 8711 -- on the *other* port.
 * Port 9000 accepts `EBXF + body`, and the 8711 re-frames it into EBFS slots
 * that restate the whole identity per chunk.  That is ebfs_ingress.c's path, not
 * this one, and it is where the §5.2 cross-check against the offer happens.
 *
 * ---------------------------------------------------------------------------
 * HOW A FILE AND A PREVIEW FRAME ARE TOLD APART -- BY SESSION, NOT BY WIRE
 * ---------------------------------------------------------------------------
 * A JPGS slot carries frame geometry and nothing else: no name, no type, no
 * whole-file CRC.  So the wire cannot say whether a frame is a wallpaper being
 * stored or a preview frame being displayed, and sniffing the content would be
 * guessing -- doubly so given both are guaranteed to be valid JPEGs.
 *
 * (EBFS, added in SPI v2.2, *can* say: it has its own magic and restates the
 * identity per chunk.  A file uploaded on port 9000 therefore never reaches this
 * file at all.  The routing below still admits JPGS-to-file because port 5004
 * remains able to carry a bare-JPEG wallpaper, and a phone that has not moved to
 * 9000 must keep working.)
 *
 * The answer is the same rule the EBXF and EBXS headers already used for their
 * shared "EBXF" magic: *whichever BLE offer opened the session decides*.
 * 0x10 XFER_OFFER -> xfer_session -> store it.  0x08 STREAM_OFFER
 * -> stream_session -> display it.  The two sessions are mutually exclusive
 * (one radio, one SoftAP, one TCP port), so at most one is non-IDLE and the
 * routing is unambiguous.  If neither is, the frame is unsolicited and is
 * dropped -- deliberately not "guess it is preview", because a stray frame
 * rendered over the UI is worse than one that never appears.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS VERIFIED HERE, AND WHAT IS VERIFIED ELSEWHERE
 * ---------------------------------------------------------------------------
 * Here (sec.5.2, per slot):  magic, version, payload bounds, per-slot payload
 * CRC32 (offset 28 -- NOT offset 12, that is ATMC's), START on the first slot,
 * Frame Sequence and Total Size constant across the frame, Offset and Chunk
 * Index strictly consecutive, END with Offset+PayloadSize == TotalSize.  Any
 * violation discards the whole in-progress frame and waits for the next START.
 *
 * Downstream: the file's *whole-file* CRC32 is xfer_session's job (it compares
 * against the 0x10 offer's value before committing to flash).  The preview path
 * has no second CRC any more -- EBXS's per-frame one is gone with the header,
 * and the per-slot CRC here is finer-grained than it was, so nothing is lost.
 * A per-slot CRC catches SPI corruption, which is a different failure from
 * "the phone sent us the wrong file".
 *
 * ---------------------------------------------------------------------------
 * BACK-PRESSURE, AND WHY THE PAYLOAD STILL HOPS TO l2_task
 * ---------------------------------------------------------------------------
 * The sink runs on the transport thread with B2W already low, and the 8711
 * cannot clock another slot until we return and the transport re-arms.  So the
 * time spent in here is natural flow control rather than a race: a slow flash
 * write delays the next slot instead of losing it.
 *
 * That makes it tempting to call xfer_session_on_tcp_data() straight from this
 * callback.  It would be wrong.  Session state is owned by l2_task and has no
 * locks by design -- the 100 ms tick that expires the RECV / overall deadlines
 * runs there, and l2_task is priority 3 against the transport thread's 5, so it
 * preempts.  A direct call could therefore land in the middle of
 * fail_and_reset() and append to a write handle that was just aborted.
 *
 * So the payload is handed over with ebadge_task_post_call() and this thread
 * *waits* for l2_task to finish with it.  The wait is what makes the borrow
 * safe: the buffer is the transport's slot, valid only for the duration of the
 * callback, so returning before l2_task has consumed it would hand it a pointer
 * into a buffer about to be re-armed for DMA.  Waiting also preserves the
 * back-pressure above rather than defeating it -- the delay simply happens on
 * this thread instead of inside it, and no copy or allocation is needed for a
 * payload that arrives ~16 times per frame.
 *
 * Two things follow from the back-pressure, and they are the reason this file
 * does not do more than it does:
 *
 *  - The multi-block NOR erase belongs in wp_begin (driven from the WAIT_STA ->
 *    RECV edge, with no slot in flight), never here.  ebadge_port_storage.c
 *    keeps it there.
 *  - Tier-2 flow control for the *preview* path (sec.5.1: after the END slot
 *    B2W must stay LOW until JPU decode and LCDC display have finished,
 *    because the 8711 blocks on that rising edge before answering "DONE <seq>"
 *    to the phone) is a transport-layer change in wifi_8711_xfer, not something
 *    this file can do.  Until a decoder is actually wired that is moot: nothing
 *    here blocks on display, so the window never stalls.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "jpgs_ingress.h"
#include "xfer_session.h"
#include "stream_session.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include <zephyr/kernel.h>
#include "wifi_8711.h"
#include "wifi_8711_at.h"
#include "spi_at_protocol.h"

/*----------------------------------------------------------------------------*
 *  JPGS header field offsets (sec.5.1).  All little-endian.
 *
 *  Spelled out rather than reusing SPI_AT_OFF_* : those are ATMC's, and the two
 *  layouts disagree in exactly the place that matters most -- the payload CRC32
 *  is at offset 28 here and at offset 12 there.
 *----------------------------------------------------------------------------*/
#define JPGS_OFF_MAGIC        0U
#define JPGS_OFF_VERSION      4U
#define JPGS_OFF_FLAGS        5U
#define JPGS_OFF_HDR_SIZE     6U
#define JPGS_OFF_FRAME_SEQ    8U
#define JPGS_OFF_TOTAL_SIZE  12U
#define JPGS_OFF_OFFSET      16U
#define JPGS_OFF_PAYLOAD_SZ  20U
#define JPGS_OFF_CHUNK_IDX   22U
#define JPGS_OFF_CHUNK_CNT   24U
#define JPGS_OFF_PAYLOAD_CRC 28U

/*----------------------------------------------------------------------------*
 *  Reassembly state
 *
 *  There is no frame buffer: payload is forwarded chunk-wise to the session as
 *  each slot arrives, so only the *bookkeeping* needed to police sec.5.2 lives
 *  here.  That is deliberate -- a 60 KiB buffer would cost more RAM than the
 *  whole protocol stack, and both consumers already stream (xfer_session appends
 *  to flash, stream_session hands chunks to its sink).  It does mean a frame
 *  that fails validation half way has already had its good prefix forwarded;
 *  the session layer's own CRC is what catches that, and for the file path a
 *  failed CRC means no commit, so nothing partial ever becomes visible.
 *----------------------------------------------------------------------------*/

/** Which session a frame in progress is being routed to. */
typedef enum
{
    JPGS_DEST_NONE = 0,     /* no frame in progress                          */
    JPGS_DEST_FILE,         /* 0x10 opened it -> xfer_session, store         */
    JPGS_DEST_STREAM,       /* 0x08 opened it -> stream_session, display     */
} jpgs_dest_t;

typedef struct
{
    bool        in_frame;       /* a START was seen and not yet invalidated  */
    jpgs_dest_t dest;           /* fixed at START, so a mid-frame session
                                 * change cannot split one frame in two      */
    uint32_t    frame_seq;
    uint32_t    total_size;
    uint32_t    next_offset;    /* Offset the next slot must carry           */
    uint16_t    next_chunk;     /* Chunk Index the next slot must carry      */
} jpgs_asm_t;

static jpgs_asm_t s_asm;

/* Lifetime counters -- they span sessions and keep counting when nothing is
 * listening, which is what makes them useful for "is the link alive at all?". */
static uint32_t s_slots;
static uint32_t s_frames_ok;
static uint32_t s_frames_dropped;
static uint32_t s_slots_unrouted;

/*----------------------------------------------------------------------------*
 *  Helpers
 *----------------------------------------------------------------------------*/

/** Abandon the frame in progress and wait for the next START (sec.5.2). */
static void frame_discard(const char *why)
{
    if (s_asm.in_frame)
    {
        s_frames_dropped++;
        EBADGE_WARN2("jpgs: frame seq=%u discarded (%s)",
                     (unsigned)s_asm.frame_seq, why);
    }
    else
    {
        /* Not in a frame: this is a stray non-START slot, i.e. the tail of a
         * frame we already gave up on.  Expected during recovery, so it is not
         * worth a line of its own. */
        EBADGE_LOG1("jpgs: slot ignored while resyncing (%s)", why);
    }
    memset(&s_asm, 0, sizeof(s_asm));
}

/**
 * Decide where a *new* frame goes.  Called only on a START slot, so the choice
 * is stable for the whole frame even if a session ends mid-frame.
 */
static jpgs_dest_t route_new_frame(void)
{
    /* File first: xfer_session is the one with a deadline and a half-written
     * flash file at stake, so if both were somehow non-IDLE it is the one that
     * must not be starved.  In practice the offer paths make that impossible. */
    if (xfer_session_state() != XFER_SESSION_IDLE)
    {
        return JPGS_DEST_FILE;
    }
    if (stream_session_state() != STREAM_SESSION_IDLE)
    {
        return JPGS_DEST_STREAM;
    }
    return JPGS_DEST_NONE;
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
    jpgs_dest_t    dest;
    /* Frame geometry, for the stream path only: it is what the EBXS header
     * would have carried, and the sink needs it to know where in the frame a
     * chunk belongs.  The file path gets its geometry from the BLE offer. */
    uint32_t       offset;
    uint32_t       total;
    bool           is_last;
} jpgs_handoff_t;

static jpgs_handoff_t s_handoff;
static K_SEM_DEFINE(handoff_done, 0, 1);

/** Runs on l2_task, where touching session state is legal. */
static void deliver_on_l2(void *arg)
{
    jpgs_handoff_t *h = (jpgs_handoff_t *)arg;

    switch (h->dest)
    {
    case JPGS_DEST_FILE:
        /* Appends to flash via ebadge_port_storage; see the back-pressure note
         * in the file header for why blocking the transport here is wanted.
         * on_payload(), not on_tcp_data(): there is no EBXF header on this
         * path -- see the framing section of the file header. */
        xfer_session_on_payload(h->payload, h->len);
        break;
    case JPGS_DEST_STREAM:
        stream_session_on_frame_chunk(h->payload, h->len, h->offset,
                                      h->total, h->is_last);
        break;
    default:
        break;
    }

    /* Only now may the transport thread return and let its slot be re-armed. */
    k_sem_give(&handoff_done);
}

/**
 * Hand payload to the session that owns the frame in progress, and wait.
 *
 * The timeout is a deadlock guard, not a normal path: if l2_task is wedged, a
 * transport thread parked here forever would take the whole SPI link down with
 * it and hide the real fault.  It is generous because the work on the other
 * side includes a NOR page program.
 */
#define JPGS_HANDOFF_TIMEOUT_MS  2000

static void deliver(const uint8_t *payload, uint16_t len, bool is_last)
{
    if (s_asm.dest != JPGS_DEST_FILE && s_asm.dest != JPGS_DEST_STREAM)
    {
        return;
    }

    s_handoff.payload = payload;
    s_handoff.len     = len;
    s_handoff.dest    = s_asm.dest;
    s_handoff.offset  = s_asm.next_offset;
    s_handoff.total   = s_asm.total_size;
    s_handoff.is_last = is_last;

    k_sem_reset(&handoff_done);
    if (ebadge_task_post_call(deliver_on_l2, &s_handoff) != 0)
    {
        /* Queue full: the payload is lost, so the frame is now incomplete.
         * Discarding it is the honest outcome -- for a file the session's own
         * CRC would fail anyway, and this way it fails with a clear reason. */
        frame_discard("l2 queue full");
        return;
    }

    if (k_sem_take(&handoff_done, K_MSEC(JPGS_HANDOFF_TIMEOUT_MS)) != 0)
    {
        /* Returning now would let the slot be re-armed for DMA while l2_task
         * may still be reading it, so say so loudly: this is a real fault, not
         * a dropped frame. */
        EBADGE_ERR1("jpgs: l2 handoff timed out after %d ms",
                    JPGS_HANDOFF_TIMEOUT_MS);
        frame_discard("handoff timeout");
    }
}

/**
 * JPGS slot sink.  CONTEXT: transport thread, with B2W held low for the whole
 * call, which back-pressures the 8711.  Must not block indefinitely; a flash
 * append is acceptable (and is the point), a wait on another thread is not.
 */
static void on_jpg_slot(const uint8_t *slot, size_t len)
{
    if (slot == NULL || len < WIFI_8711_HEADER_SIZE)
    {
        return;
    }

    s_slots++;

    /* ---- header sanity (sec.5.1) ------------------------------------- */
    uint8_t  version  = slot[JPGS_OFF_VERSION];
    uint8_t  flags    = slot[JPGS_OFF_FLAGS];
    uint16_t hdr_size = spi_at_get_le16(slot + JPGS_OFF_HDR_SIZE);
    uint32_t seq      = spi_at_get_le32(slot + JPGS_OFF_FRAME_SEQ);
    uint32_t total    = spi_at_get_le32(slot + JPGS_OFF_TOTAL_SIZE);
    uint32_t offset   = spi_at_get_le32(slot + JPGS_OFF_OFFSET);
    uint16_t psize    = spi_at_get_le16(slot + JPGS_OFF_PAYLOAD_SZ);
    uint16_t cidx     = spi_at_get_le16(slot + JPGS_OFF_CHUNK_IDX);
    uint32_t pcrc     = spi_at_get_le32(slot + JPGS_OFF_PAYLOAD_CRC);

    if (version != WIFI_8711_JPG_VERSION || hdr_size != WIFI_8711_HEADER_SIZE)
    {
        frame_discard("bad version/hdr_size");
        return;
    }
    if (psize == 0U || psize > WIFI_8711_JPG_PAYLOAD_MAX ||
        (size_t)WIFI_8711_HEADER_SIZE + psize > len)
    {
        frame_discard("bad payload size");
        return;
    }

    const uint8_t *payload = slot + WIFI_8711_HEADER_SIZE;

    /* Payload CRC32 at offset 28, covering payload only -- never the header.
     * Checked before any state is updated so a corrupt slot cannot advance the
     * expected offset. */
    if (pcrc != spi_at_crc32(payload, psize))
    {
        frame_discard("payload crc");
        return;
    }

    bool is_start = (flags & WIFI_8711_JPG_FLAG_START) != 0U;
    bool is_end   = (flags & WIFI_8711_JPG_FLAG_END)   != 0U;

    /* ---- START: begin a frame (sec.5.2 first-chunk rules) ------------ */
    if (is_start)
    {
        if (s_asm.in_frame)
        {
            /* A new START while a frame is open means the previous one never
             * finished -- the sender gave up on it, so we do too. */
            frame_discard("new START mid-frame");
        }
        if (offset != 0U || cidx != 0U)
        {
            EBADGE_WARN2("jpgs: START with offset=%u chunk=%u, ignored",
                         (unsigned)offset, (unsigned)cidx);
            return;
        }
        if (total == 0U || total > WIFI_8711_JPEG_FRAME_MAX)
        {
            EBADGE_WARN1("jpgs: START total_size=%u out of range, ignored",
                         (unsigned)total);
            return;
        }

        jpgs_dest_t dest = route_new_frame();
        if (dest == JPGS_DEST_NONE)
        {
            /* Nobody asked for this.  Counted rather than logged per slot: an
             * 8711 left streaming after a session ended would otherwise bury
             * the log at ~16 slots per frame. */
            s_slots_unrouted++;
            if ((s_slots_unrouted % 64U) == 1U)
            {
                EBADGE_LOG1("jpgs: frame with no session to route to (x%u)",
                            (unsigned)s_slots_unrouted);
            }
            return;
        }

        s_asm.in_frame    = true;
        s_asm.dest        = dest;
        s_asm.frame_seq   = seq;
        s_asm.total_size  = total;
        s_asm.next_offset = 0U;
        s_asm.next_chunk  = 0U;

        EBADGE_LOG3("jpgs: frame seq=%u size=%u -> %s", (unsigned)seq,
                    (unsigned)total,
                    (dest == JPGS_DEST_FILE) ? "file" : "stream");
    }
    else if (!s_asm.in_frame)
    {
        /* Mid-frame slot with no frame open: either we discarded the frame and
         * are waiting for the next START, or the link came up mid-frame. */
        s_slots_unrouted++;
        return;
    }

    /* ---- continuation constraints (sec.5.2) -------------------------- */
    if (seq != s_asm.frame_seq || total != s_asm.total_size)
    {
        frame_discard("seq/total changed mid-frame");
        return;
    }
    if (offset != s_asm.next_offset || cidx != s_asm.next_chunk)
    {
        frame_discard("out-of-order chunk");
        return;
    }
    if ((uint64_t)offset + psize > s_asm.total_size)
    {
        frame_discard("payload overruns total_size");
        return;
    }
    if (is_end && ((uint64_t)offset + psize != s_asm.total_size))
    {
        frame_discard("END short of total_size");
        return;
    }
    if (!is_end && ((uint64_t)offset + psize == s_asm.total_size))
    {
        /* The frame is complete but END was not set.  Trusting the byte count
         * over the flag would leave the sender and us disagreeing about where
         * the next frame starts, which resyncs badly. */
        frame_discard("complete without END flag");
        return;
    }

    /* ---- forward, then advance ---------------------------------------
     *
     * deliver() may itself discard the frame (queue full / handoff timeout), so
     * re-check before advancing: otherwise the counters below would resurrect a
     * frame that was just abandoned. */
    deliver(payload, psize, is_end);
    if (!s_asm.in_frame)
    {
        return;
    }

    s_asm.next_offset += psize;
    s_asm.next_chunk++;

    if (is_end)
    {
        s_frames_ok++;
        EBADGE_LOG2("jpgs: frame seq=%u complete (%u bytes)",
                    (unsigned)s_asm.frame_seq, (unsigned)s_asm.next_offset);
        memset(&s_asm, 0, sizeof(s_asm));
    }
}

void jpgs_ingress_init(void)
{
    memset(&s_asm, 0, sizeof(s_asm));
    wifi_8711_at_set_jpg_sink(on_jpg_slot);
    EBADGE_LOG("jpgs: sink registered (reassembling, routed by session)");
}

void jpgs_ingress_reset(void)
{
    if (s_asm.in_frame)
    {
        EBADGE_LOG1("jpgs: reset, dropping frame seq=%u",
                    (unsigned)s_asm.frame_seq);
    }
    memset(&s_asm, 0, sizeof(s_asm));
}

uint32_t jpgs_ingress_slots(void)         { return s_slots; }
uint32_t jpgs_ingress_frames_ok(void)     { return s_frames_ok; }
uint32_t jpgs_ingress_frames_dropped(void) { return s_frames_dropped; }

#else /* !CONFIG_WIFI_8711 */

void jpgs_ingress_init(void)
{
    /* No SPI link to the 8711 on this build, so no stream can arrive.  Silent
     * rather than a warning: this is a build configuration, not a fault. */
}

void jpgs_ingress_reset(void)             { }
uint32_t jpgs_ingress_slots(void)         { return 0U; }
uint32_t jpgs_ingress_frames_ok(void)     { return 0U; }
uint32_t jpgs_ingress_frames_dropped(void) { return 0U; }

#endif /* CONFIG_WIFI_8711 */
