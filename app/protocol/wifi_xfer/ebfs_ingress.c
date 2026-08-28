/**
 * @file    ebfs_ingress.c
 * @brief   File-upload byte counter.  The framing it used to do is gone.
 *
 * ---------------------------------------------------------------------------
 * THIS FILE USED TO BE 650 LINES AND IS NOW A HUNDRED
 * ---------------------------------------------------------------------------
 * It validated EBFS slots: a 64-byte header per 4 KiB slot with a session id, a
 * total size, an offset, a chunk index and count, a CRC32 over the payload, and
 * START/END flags -- all produced by custom 8711 firmware that chopped the TCP
 * stream into slots on our behalf.  Every one of those fields, and every rule
 * about them, described the SLOT rather than the file, and the slots do not exist
 * any more.
 *
 * The standard firmware forwards the TCP stream as it arrives.  So:
 *
 *   the per-slot CRC32     gone.  It caught SPI corruption between the two chips;
 *                          the SPI transport's own integrity now covers that hop,
 *                          and TCP covers the phone-to-8711 one.  The whole-file
 *                          CRC32 against the BLE offer is untouched and is what
 *                          actually protects the stored file.
 *   the continuity rules   gone.  Offset/chunk-index arithmetic policed a framing
 *                          layer.  TCP delivers in order or not at all, which is
 *                          the property those rules were checking for.
 *   START/END              gone.  There is no flag for "first bytes"; the first
 *                          bytes are simply the first bytes.  Completion is the
 *                          byte count reaching the size the EBXF header declared,
 *                          which xfer_session was always the one to notice.
 *   the K_SEM handoff      gone, and this is the one worth understanding -- see
 *                          the threading note below.
 *
 * What is left is a byte count and a pass-through, which is enough to earn a file
 * of its own only because the counters are the bring-up instrument that answers
 * "is anything arriving at all?" separately from "is the session accepting it?".
 *
 * ---------------------------------------------------------------------------
 * WHY THE HANDOFF APPARATUS WENT AWAY
 * ---------------------------------------------------------------------------
 * The old slot sink ran on the transport thread, holding a pointer into a DMA
 * buffer that would be re-armed the moment it returned.  So it could not simply
 * call into the session -- l2_task owns session state and preempts the transport
 * thread -- and it could not copy either, at ~16 slots per file with no spare
 * PSRAM bandwidth.  What it did instead was post the pointer to l2_task and BLOCK
 * on a semaphore until l2_task had consumed it, borrowing the buffer across the
 * thread boundary.  That is where the 2000 ms deadlock guard and the static
 * descriptor came from.
 *
 * None of it is needed now, because the copy happens one layer up: xfer_session's
 * own on_data callback -- the one it registers with port_tcp -- heap-copies and
 * posts.  By the time this module is reached, the bytes are on the heap and we are
 * on l2_task.  The copy costs an allocation per delivery and buys the deletion of
 * a cross-thread buffer borrow, which is the trade any reader would make.
 *
 * ---------------------------------------------------------------------------
 * THE 40-BYTE EBXF HEADER IS STILL IN FRONT OF THE FILE
 * ---------------------------------------------------------------------------
 * The phone uploads `EBXF 40B header + body` (spec sec.5.2) and nothing between it
 * and here strips that header, so the first 40 bytes of the stream are not file
 * content.  xfer_session_on_tcp_data() is the function that knows this: it
 * accumulates the header across deliveries, parses it, runs the sec.5.2 identity
 * cross-check against the BLE offer, and only then starts feeding the body to the
 * CRC and the receive cache.
 *
 * on_tcp_data(), therefore -- NOT on_payload().  The distinction is invisible at
 * the call site and total in effect: on_payload() would treat those 40 bytes as
 * the start of the JPEG, storing a corrupt file whose CRC fails with nothing in
 * the log pointing at why.
 *
 * ---------------------------------------------------------------------------
 * NO FLASH ON THIS PATH
 * ---------------------------------------------------------------------------
 * Unchanged, and still worth stating: xfer_session buffers the whole file in
 * PSRAM, verifies it, sends the EBXR, and only then erases and writes.  The
 * original reason was the 8711's 1000 ms slot READY budget, which a multi-sector
 * NOR erase cannot fit inside.  The slots are gone but the constraint is not: the
 * 8711 still has a finite receive buffer per link, and a long stall here still
 * back-pressures TCP and then loses bytes.  The rule stays as it was -- the
 * receive path never touches flash.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ebfs_ingress.h"
#include "xfer_session.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)

/* Lifetime counters -- they span sessions and keep counting when nothing is
 * listening, which is what makes them useful for "is the link alive at all?". */
static uint32_t s_bytes;
static uint32_t s_files_ok;
static uint32_t s_files_dropped;
static uint32_t s_bytes_unrouted;

/** True while bytes of one upload are being forwarded.  Only so the counters can
 *  tell a completed file from an abandoned one -- the session tracks everything
 *  that actually matters. */
static bool s_in_file;

void ebfs_ingress_on_tcp_data(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U)
    {
        return;
    }
    s_bytes += len;

    /* RECV only.  WAIT_STA used to be admitted here as well, because the join was
     * observed by a 15 s AT+WLSTATE poll that the phone routinely outran -- data
     * arrived before we knew a station had associated, and requiring RECV dropped
     * the first slots of every upload.
     *
     * That cannot happen now: the join edge IS the TCP connection (there are no
     * association events in the standard command set, so port_tcp reports the
     * seed-connect instead), and the connect event precedes the first byte on the
     * same link by construction.  So WAIT_STA at this point would mean the connect
     * event was lost, not that we are behind -- and promoting the session on data
     * would be papering over that rather than racing it. */
    xfer_session_state_t st = xfer_session_state();
    if (st != XFER_SESSION_RECV)
    {
        /* Counted rather than logged per delivery: an upload still in flight when
         * a session ends would otherwise bury the log. */
        s_bytes_unrouted += len;
        if (s_in_file)
        {
            s_files_dropped++;
            s_in_file = false;
            EBADGE_WARN1("ebfs: upload abandoned mid-file (state=%d)", (int)st);
        }
        return;
    }

    s_in_file = true;

    /* on_tcp_data(), not on_payload() -- the EBXF header is still in front.  See
     * the file header.
     *
     * A non-zero return means the session has already cut the connection and
     * emitted 0x16, so there is nothing to do but stop counting this file: a
     * rejection is the session's decision and it has acted on it in full. */
    if (xfer_session_on_tcp_data(data, len) != 0)
    {
        s_files_dropped++;
        s_in_file = false;
        return;
    }

    /* Completion is not observable here.  xfer_session sees the byte count reach
     * the size the EBXF header declared, verifies, commits and leaves RECV -- all
     * inside the call above -- so "the file finished" and "the file was abandoned"
     * look identical from this side.  The counter is therefore advanced on the
     * state leaving RECV cleanly, which is what the IDLE/COMPLETING check on the
     * next delivery cannot distinguish either.
     *
     * Rather than guess, ask: xfer_session_files_ok() is the authority and this
     * counter simply mirrors the transition it can see. */
    if (xfer_session_state() != XFER_SESSION_RECV)
    {
        s_files_ok++;
        s_in_file = false;
        EBADGE_LOG("ebfs: upload complete (session left RECV)");
    }
}

void ebfs_ingress_init(void)
{
    s_in_file = false;
    /* Registers nothing.  The old init installed a slot sink on the 8711 AT
     * layer; the path is now port_tcp -> xfer_session -> here, and xfer_session
     * is what registers with port_tcp.  Kept as a call site so
     * ebadge_task_init()'s wiring list still names this module. */
    EBADGE_LOG("ebfs: upload path ready (file port -> xfer_session)");
}

void ebfs_ingress_reset(void)
{
    if (s_in_file)
    {
        EBADGE_LOG("ebfs: reset while an upload was in flight");
        s_files_dropped++;
    }
    s_in_file = false;
}

uint32_t ebfs_ingress_slots(void)         { return s_bytes; }
uint32_t ebfs_ingress_files_ok(void)      { return s_files_ok; }
uint32_t ebfs_ingress_files_dropped(void) { return s_files_dropped; }

#else /* !CONFIG_WIFI_8711 */

void ebfs_ingress_on_tcp_data(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
}

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
