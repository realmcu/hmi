/**
 * @file    jpgs_ingress.c
 * @brief   Finds preview-frame boundaries in a raw TCP byte stream.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE USED TO BE, AND WHY ALMOST NONE OF IT SURVIVED
 * ---------------------------------------------------------------------------
 * It used to reassemble JPGS slots: a 32-byte header per 4 KiB slot carrying a
 * frame sequence number, a total size, an offset, a chunk index, and a CRC32 over
 * the payload -- all of it produced by custom 8711 firmware that had parsed the
 * phone's "JPG <size> <seq>" line and knew where each frame began and ended.
 *
 * The standard firmware does none of that.  It is a network card: the preview
 * port delivers exactly the bytes the phone wrote, in whatever sizes they happen
 * to arrive, with no framing of any kind.  So every field that file validated is
 * gone, along with the validation:
 *
 *   frame sequence     no longer exists.  Frames are delimited by their own
 *                      content now, and a sequence number was only ever the
 *                      8711 restating what it had already found.
 *   total size         not known until FF D9 arrives.  This is the change that
 *                      forces a buffer -- see below.
 *   offset/chunk index no longer exists.  TCP does not reorder or gap; a stream
 *                      that arrives at all arrives in order, which is exactly
 *                      what the continuity arithmetic was checking.
 *   per-slot CRC32     no longer exists.  TCP's own checksum covers the phone-to
 *                      -8711 hop and the SPI transport covers the 8711-to-us hop,
 *                      so the property is still held -- just not by us.
 *
 * What remains is the one thing the 8711 no longer does: deciding where a frame
 * starts and stops.
 *
 * ---------------------------------------------------------------------------
 * WHY THERE IS A WHOLE-FRAME BUFFER NOW, WHICH THERE DELIBERATELY WAS NOT BEFORE
 * ---------------------------------------------------------------------------
 * The old file went out of its way NOT to buffer: it forwarded each slot's
 * payload as it arrived and passed the geometry along from the slot header, on the
 * grounds that a 60 KiB frame buffer would cost more RAM than the rest of the
 * protocol stack.  That reasoning was sound and is now inapplicable, because the
 * geometry it was passing along does not exist until the frame is complete.
 *
 * stream_session_on_frame_chunk() needs (offset, frame_size, is_last) per chunk.
 * frame_size is only knowable at FF D9.  So either the sink learns to accept a
 * frame of unknown length -- which a JPEG decoder cannot, it needs to know how
 * much to read -- or the frame is buffered until its length is known.  It is
 * buffered, in PSRAM rather than SRAM, because 128 KB of SRAM does not exist on
 * this chip and PSRAM's slowness does not matter for something written once and
 * read once per frame.
 *
 * The consequence worth stating: the frame reaches the sink only once it is
 * COMPLETE, so a decoder is never handed a prefix of unknown length.  It still
 * arrives in slices -- the sink's len is a uint16_t and a frame may be larger than
 * 64 KB -- but every slice carries the final frame_size, which is the property the
 * old chunk-by-chunk delivery could not offer.
 *
 * ---------------------------------------------------------------------------
 * THE SCANNER, AND THE ONE WAY IT IS KNOWN TO BE FOOLABLE
 * ---------------------------------------------------------------------------
 * SOI is FF D8, EOI is FF D9, and the scan is for those two byte pairs.  Both may
 * straddle a delivery boundary, so the last byte of every buffer is remembered.
 *
 * The known false positive is an EXIF thumbnail: a JPEG whose APP1 segment
 * contains a second, smaller JPEG carries an inner FF D8 ... FF D9 pair, and a
 * naive scan ends the frame at the thumbnail's EOI.  What that produces is a
 * truncated frame that fails to decode, followed by a resynchronisation on the
 * next real SOI -- so it degrades to a dropped frame rather than to a wedge.
 *
 * It is not defended against here, because doing so properly means walking the
 * JPEG segment structure (read each marker's length, skip its payload, and only
 * then look for EOI), and that is a decoder's job rather than a transport's.  What
 * makes the exposure acceptable is that the phone generates these frames for
 * preview: it is encoding them itself, at a size it chose, and an encoder emitting
 * EXIF thumbnails into a live preview stream would be doing something strange.
 * If a real phone turns out to do it, the fix is segment walking and this comment
 * is where to start.
 *
 * A second, milder case: a byte pair that happens to fall inside entropy-coded
 * data cannot be FF D9, because the JPEG standard requires an encoder to byte-stuff
 * a literal FF in that data as FF 00.  So the scan is safe there by construction.
 *
 * ---------------------------------------------------------------------------
 * ROUTING: STILL BY SESSION, NO LONGER BY PORT ALONE
 * ---------------------------------------------------------------------------
 * This module is registered as the PREVIEW port's data path only, which the old
 * one was not -- it was the sink for every JPGS slot regardless of which session
 * wanted it, so it had to ask.  The port is now the answer: bytes on the stream
 * port belong to stream_session, and a file uploaded on the file port never
 * reaches this file at all.
 *
 * The old JPGS-to-file route is therefore gone.  It existed because port 5004
 * could carry a bare-JPEG wallpaper from a phone that had not moved to the file
 * port, and with the standard firmware there is no such thing: the file port
 * exists, the phone is told which port to use in the 0x13 AP_INFO, and a wallpaper
 * arriving on the preview port would be a phone ignoring what it was told.
 *
 * ---------------------------------------------------------------------------
 * THREADING
 * ---------------------------------------------------------------------------
 * stream_session installs its own on_data with port_tcp, and that callback
 * heap-copies and posts to l2_task.  So by the time this module's entry point
 * runs, it is already on l2_task and there is nothing to marshal -- which is why
 * the K_SEM handoff, the static descriptor, and the 2000 ms deadlock guard that
 * used to be here are all gone.  They existed to move slot payload off the
 * transport thread without copying it; the copy now happens one layer up, where
 * it also serves the file path.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "jpgs_ingress.h"
#include "stream_session.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include "../port/ebadge_psram_map.h"

/** JPEG markers.  Two bytes each, and either may straddle a delivery. */
#define JPEG_MARK_FF   0xFFU
#define JPEG_SOI_2ND   0xD8U
#define JPEG_EOI_2ND   0xD9U

/** Where the frame is assembled.  PSRAM, non-cacheable, see ebadge_psram_map.h.
 *
 *  A pointer to a fixed address rather than an array, for the same reason the
 *  receive cache is: 128 KB of .bss would not fit, and the address has to agree
 *  with the map's BUILD_ASSERTs, which only work on the macro. */
static uint8_t *const s_frame = (uint8_t *)EB_PSRAM_STREAM_FRAME_BASE;
#define JPGS_FRAME_MAX   EB_PSRAM_STREAM_FRAME_SIZE

/*----------------------------------------------------------------------------*
 *  Scanner state
 *
 *  Small enough to be worth spelling out: everything needed to resume a scan
 *  mid-marker across an arbitrary delivery boundary.
 *----------------------------------------------------------------------------*/
typedef struct
{
    bool     in_frame;      /* an SOI was seen and no EOI yet                 */
    uint32_t got;           /* bytes buffered, including the SOI              */
    bool     pending_ff;    /* the previous byte was FF; it may open a marker */
    bool     overflow;      /* the frame outgrew the buffer -- resync on SOI  */
} jpgs_scan_t;

static jpgs_scan_t s_scan;

/* Lifetime counters -- they span sessions and keep counting when nothing is
 * listening, which is what makes them useful for "is the link alive at all?". */
static uint32_t s_bytes;
static uint32_t s_frames_ok;
static uint32_t s_frames_dropped;

/*----------------------------------------------------------------------------*
 *  Helpers
 *----------------------------------------------------------------------------*/

/** Abandon the frame in progress and wait for the next SOI. */
static void frame_discard(const char *why)
{
    if (s_scan.in_frame)
    {
        s_frames_dropped++;
        EBADGE_WARN2("jpgs: frame dropped after %u bytes (%s)",
                     (unsigned)s_scan.got, why);
    }
    memset(&s_scan, 0, sizeof(s_scan));
}

/** One byte into the frame buffer, or note the overflow.
 *
 *  Overflow is sticky rather than an immediate discard, because the frame is not
 *  resynchronisable until the next SOI: dropping the state now would let the very
 *  next FF D8 inside this oversized frame's entropy data -- if any survived the
 *  byte-stuffing rule -- start a bogus frame.  Waiting for a real boundary is the
 *  same policy the rest of the scanner follows. */
static void frame_put(uint8_t b)
{
    if (s_scan.got < JPGS_FRAME_MAX)
    {
        s_frame[s_scan.got++] = b;
        return;
    }
    if (!s_scan.overflow)
    {
        s_scan.overflow = true;
        EBADGE_WARN1("jpgs: frame exceeds %u bytes -- dropping to the next SOI",
                     (unsigned)JPGS_FRAME_MAX);
    }
}

/** A complete frame is in the buffer.  Hand it over and start again. */
static void frame_complete(void)
{
    if (s_scan.overflow)
    {
        /* The EOI we just found belongs to a frame whose middle we threw away,
         * so what is in the buffer is a prefix and a suffix with a hole between
         * them.  Counted as dropped rather than delivered: a decoder handed this
         * would fail in a way that reads as corruption on the wire. */
        frame_discard("overflowed");
        return;
    }

    s_frames_ok++;
    if (s_frames_ok == 1U || (s_frames_ok % 30U) == 0U)
    {
        EBADGE_LOG2("jpgs: frame #%u complete, %u bytes",
                    (unsigned)s_frames_ok, (unsigned)s_scan.got);
    }

    /* Delivered in slices, because the sink's len is a uint16_t and the buffer is
     * 128 KB.  32 KB per slice rather than 65535 so the arithmetic has obvious
     * headroom against the type rather than sitting exactly on its edge -- an
     * off-by-one there would silently deliver a zero-length final slice.
     *
     * Every slice carries the FINAL frame_size, which is the whole point of having
     * buffered: the old path passed a size taken from the JPGS header, and the
     * decoder had no way to know a frame was short until it ran out of bytes. */
    uint32_t off = 0U;
    while (off < s_scan.got)
    {
        uint32_t left  = s_scan.got - off;
        uint16_t slice = (left > 0x8000U) ? 0x8000U : (uint16_t)left;

        stream_session_on_frame_chunk(s_frame + off, slice, off, s_scan.got,
                                      (off + slice) >= s_scan.got);
        off += slice;
    }

    memset(&s_scan, 0, sizeof(s_scan));
}

/*----------------------------------------------------------------------------*
 *  Entry point
 *----------------------------------------------------------------------------*/

/**
 * Raw preview-port bytes.  CONTEXT: l2_task -- stream_session's on_data has
 * already copied and posted them, so session state may be touched freely.
 *
 * The loop is byte-at-a-time rather than a memchr for FF, which is slower and
 * chosen anyway: a preview frame is tens of KB at a handful of frames per second,
 * so this is thousands of bytes per 100 ms tick on a 100 MHz core -- not a budget
 * worth optimising against the cost of a scanner whose resume-across-boundary
 * logic has two forms.  The single-byte form is the one that is obviously correct
 * at every boundary, which is where this kind of code goes wrong.
 */
void jpgs_ingress_on_tcp_data(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U)
    {
        return;
    }
    s_bytes += len;

    for (uint16_t i = 0U; i < len; i++)
    {
        uint8_t b = data[i];

        if (s_scan.pending_ff)
        {
            s_scan.pending_ff = false;

            if (b == JPEG_SOI_2ND)
            {
                /* SOI.  If a frame was already open this is the sender having
                 * abandoned it -- the previous frame's EOI never arrived -- so
                 * take the new one and count the loss. */
                if (s_scan.in_frame)
                {
                    frame_discard("new SOI mid-frame");
                }
                s_scan.in_frame = true;
                s_scan.got      = 0U;
                s_scan.overflow = false;
                frame_put(JPEG_MARK_FF);
                frame_put(b);
                continue;
            }

            if (b == JPEG_EOI_2ND && s_scan.in_frame)
            {
                frame_put(JPEG_MARK_FF);
                frame_put(b);
                frame_complete();
                continue;
            }

            /* Some other marker, or data.  The FF belongs to the frame as much as
             * anything else does, so it goes in and so does this byte -- and the
             * byte falls through to the checks below rather than being consumed
             * here, because it may itself be the FF of the next marker. */
            if (s_scan.in_frame)
            {
                frame_put(JPEG_MARK_FF);
            }
        }

        if (b == JPEG_MARK_FF)
        {
            /* Deferred: whether this byte is part of a marker or a stuffed
             * literal is not knowable until the next one.  Held out of the buffer
             * until then, which is why every path above that resolves a pending
             * FF writes it explicitly. */
            s_scan.pending_ff = true;
            continue;
        }

        if (s_scan.in_frame)
        {
            frame_put(b);
        }
        /* Not in a frame and not an FF: bytes before the first SOI, or the tail of
         * a frame we abandoned.  Silently dropped -- this is the normal state
         * between frames and during resynchronisation. */
    }
}

void jpgs_ingress_init(void)
{
    memset(&s_scan, 0, sizeof(s_scan));
    /* Nothing to register.  The old init installed a slot sink on the 8711 AT
     * layer; the path is now port_tcp -> stream_session -> here, and
     * stream_session is what registers with port_tcp.  Kept as a call site so
     * ebadge_task_init()'s wiring list still names this module, and so the
     * counters are provably zeroed before any byte can arrive. */
    EBADGE_LOG("jpgs: JPEG boundary scanner ready (preview port)");
}

void jpgs_ingress_reset(void)
{
    if (s_scan.in_frame)
    {
        EBADGE_LOG1("jpgs: reset, dropping a partial frame (%u bytes)",
                    (unsigned)s_scan.got);
    }
    memset(&s_scan, 0, sizeof(s_scan));
}

uint32_t jpgs_ingress_slots(void)          { return s_bytes; }
uint32_t jpgs_ingress_frames_ok(void)      { return s_frames_ok; }
uint32_t jpgs_ingress_frames_dropped(void) { return s_frames_dropped; }

#else /* !CONFIG_WIFI_8711 */

void jpgs_ingress_on_tcp_data(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
}

void jpgs_ingress_init(void)
{
    /* No SPI link to the 8711 on this build, so no stream can arrive.  Silent
     * rather than a warning: this is a build configuration, not a fault. */
}

void jpgs_ingress_reset(void)              { }
uint32_t jpgs_ingress_slots(void)          { return 0U; }
uint32_t jpgs_ingress_frames_ok(void)      { return 0U; }
uint32_t jpgs_ingress_frames_dropped(void) { return 0U; }

#endif /* CONFIG_WIFI_8711 */
