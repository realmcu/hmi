/**
 * @file    ebfs_ingress.c
 * @brief   EBFS slot validation, cross-plane identity check, and hand-off.
 *
 * ---------------------------------------------------------------------------
 * WHAT ARRIVES HERE
 * ---------------------------------------------------------------------------
 * The phone uploads to the 8711 on TCP port 9000 as `EBXF 40B header + body`.
 * The 8711 does not buffer the file: it parses that header, then re-frames the
 * body into 4096-byte EBFS slots on the SPI link, restating the identity from
 * the EBXF header in every slot (SPI spec v2.2 §6).  So:
 *
 *     phone --Wi-Fi/TCP:9000--> 8711FA --SPI EBFS slots--> 8773G (us)
 *
 * The EBXF header never reaches this chip as bytes, which is why xfer_session's
 * EBXF parser has no caller.  Nothing is lost by that: EBFS carries strictly
 * more (a session id, a chunk index, a per-chunk CRC) and carries it repeatedly.
 *
 * ---------------------------------------------------------------------------
 * WHY THE IDENTITY CHECK HAPPENS ON THE FIRST SLOT
 * ---------------------------------------------------------------------------
 * eBadge spec §5.2 requires the data plane's size / crc32 / type / name to match
 * the BLE offer, and says a mismatch means close the connection and report 0x16.
 * Because EBFS repeats those fields on every slot, that comparison can happen
 * before the first payload byte reaches flash -- so a disagreement costs one
 * slot rather than a whole 2 MiB write followed by a CRC that was never going to
 * match.  xfer_session_check_identity() does the comparing and, on failure, has
 * already cut the stream (AT+XFERSTOP) and failed the session by the time it
 * returns.
 *
 * It is re-checked on every slot rather than only the first.  §6.2 requires the
 * fields to be identical across the file, the comparison is four integers and a
 * short string, and a sender that changes its mind mid-file is exactly the case
 * a first-slot-only check would wave through.
 *
 * ---------------------------------------------------------------------------
 * VALIDATION SPLIT
 * ---------------------------------------------------------------------------
 * Here, per slot (§6.2): magic, version, Header Size, Payload Size bounds, all
 * offsets inside the slot BEFORE payload or name is touched, the per-chunk CRC32
 * (offset 28 -- NOT offset 12, which is ATMC's, and NOT the whole-file CRC at
 * offset 32), Session ID / Total Size / Chunk Count constant across the file,
 * START on the first slot with Offset and Chunk Index zero, Offset and Chunk
 * Index strictly consecutive, END with Offset+PayloadSize == TotalSize.
 *
 * Downstream: the whole-file CRC32 is xfer_session's, compared against the BLE
 * offer's value before it commits to flash.  The two CRCs answer different
 * questions -- the per-chunk one catches SPI corruption, the whole-file one
 * catches the phone sending the wrong file -- so both are wanted.
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
 *
 * One thing is NOT like the JPGS path: §6.3 gives the EBFS END slot no
 * display-completion semantics.  The last slot completes the ordinary four-phase
 * handshake, so there is no long B2W-low window to hide the flash commit in --
 * every slot's processing back-pressures TCP directly, and the final commit
 * happens inside the 120 s the 8711 will wait for our AT+XFERACK.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ebfs_ingress.h"
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
 *  flash.  Only what is needed to police §6.2 lives here.
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
    /* Identity, restated by every slot, for the §5.2 cross-check.  Carried
     * across the hop rather than checked on this side because it compares
     * against session state that only l2_task may read. */
    uint32_t       total_size;
    uint32_t       file_crc32;
    uint8_t        file_type;
    char           name[EBFS_NAME_MAX + 1];
    bool           accepted;   /* out: false if the check failed             */
} ebfs_handoff_t;

static ebfs_handoff_t s_handoff;
static K_SEM_DEFINE(ebfs_handoff_done, 0, 1);

/** Runs on l2_task, where touching session state is legal. */
static void deliver_on_l2(void *arg)
{
    ebfs_handoff_t *h = (ebfs_handoff_t *)arg;

    /* Cross-plane check first: it can tear the session down, in which case the
     * payload must NOT be written -- the whole point of checking here is to keep
     * bytes belonging to the wrong file out of flash. */
    h->accepted = xfer_session_check_identity(h->total_size, h->file_crc32,
                                              h->file_type, h->name);
    if (h->accepted)
    {
        xfer_session_on_payload(h->payload, h->len);
    }

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
 */
static bool deliver(const uint8_t *slot, const uint8_t *payload, uint16_t len)
{
    s_handoff.payload    = payload;
    s_handoff.len        = len;
    s_handoff.total_size = spi_at_get_le32(slot + EBFS_OFF_TOTAL_SIZE);
    s_handoff.file_crc32 = spi_at_get_le32(slot + EBFS_OFF_FILE_CRC);
    s_handoff.file_type  = slot[EBFS_OFF_FILE_TYPE];
    s_handoff.accepted   = false;

    /* Name Length was bounds-checked by the caller before we got here, which is
     * what makes this copy safe (§6.2: check every bound before touching name
     * or payload). */
    uint8_t nlen = slot[EBFS_OFF_NAME_LEN];
    memcpy(s_handoff.name, slot + EBFS_OFF_NAME, nlen);
    s_handoff.name[nlen] = '\0';

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
    /* §6.1: Name Length is 1..23.  Rejected rather than clamped -- a slot whose
     * name field we cannot trust is one whose identity we cannot cross-check,
     * and the check is the reason this path exists. */
    if (nlen == 0U || nlen > EBFS_NAME_MAX)
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
    if (ccrc != spi_at_crc32(payload, psize))
    {
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
        if (total == 0U || total > WIFI_8711_FILE_SIZE_MAX)
        {
            EBADGE_WARN1("ebfs: START total_size=%u out of range, ignored",
                         (unsigned)total);
            return;
        }

        /* Only the file session can receive these; a preview stream never gets
         * EBFS slots, which is the whole reason EBFS has its own magic. */
        if (xfer_session_state() != XFER_SESSION_RECV)
        {
            /* Nobody asked for this.  Counted rather than logged per slot: an
             * 8711 left forwarding after a session ended would otherwise bury
             * the log at hundreds of slots per file. */
            s_slots_unrouted++;
            if ((s_slots_unrouted % 64U) == 1U)
            {
                EBADGE_LOG2("ebfs: file with no session to route to "
                            "(state=%d, x%u)", (int)xfer_session_state(),
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

        EBADGE_LOG3("ebfs: file session=%u size=%u chunks=%u -> store",
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
    if (!deliver(slot, payload, psize))
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
        EBADGE_LOG2("ebfs: file session=%u complete (%u bytes)",
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
