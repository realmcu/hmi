/**
 * @file    stream_sink.c
 * @brief   Copies completed preview frames into the GUI stream transport.
 *
 * ---------------------------------------------------------------------------
 * WHERE THIS SITS
 * ---------------------------------------------------------------------------
 *   phone --TCP--> 8711 --SPI--> port_tcp --> stream_session (§6 state machine)
 *                                                 |
 *                                       jpgs_ingress (FF D8..FF D9 scan)
 *                                                 |
 *                                    stream_session_on_frame_chunk()
 *                                                 |
 *                                          *** this file ***
 *                                                 |
 *                                stp_acquire_free / memcpy / stp_commit
 *                                                 |
 *                              gui_stream widget on gui_task: decode + draw
 *
 * ---------------------------------------------------------------------------
 * WHY IT COPIES, WHEN A COPY IS EXACTLY WHAT THE TRANSPORT EXISTS TO AVOID
 * ---------------------------------------------------------------------------
 * stream_transport.h is explicit that payloads never move: only descriptors
 * traverse the queues.  That property holds for a producer that can write its
 * bytes straight into a transport buffer -- which this one cannot, because the
 * frame is already whole by the time it is knowable as a frame.
 *
 * jpgs_ingress has to buffer: a JPEG's length is unknown until FF D9, and it
 * cannot ask the transport for a buffer of unknown size.  So there is one
 * PSRAM staging buffer (EB_PSRAM_STREAM_FRAME_BASE) and one copy out of it into
 * a pool buffer whose size is now known.  Removing the copy would mean giving
 * jpgs_ingress the transport, and it would still have to guess a size up front
 * and then release-and-reacquire whenever it guessed low -- more moving parts
 * for a memcpy of tens of KB out of PSRAM, once per frame at 15 fps.
 *
 * The copy is also what makes the staging buffer reusable immediately: by the
 * time this returns, jpgs_ingress may overwrite it with the next frame while
 * the committed copy waits in the ready queue for gui_task.  A zero-copy
 * arrangement would need the staging buffer to be the pool.
 *
 * ---------------------------------------------------------------------------
 * THREADING AND THE ONE THING THAT WOULD BREAK IT
 * ---------------------------------------------------------------------------
 * Producer side only, on l2_task.  stp_acquire_free() and stp_commit() are the
 * producer's two primitives and are the only ones used here in the normal path.
 * The stp_release() in stream_sink_reset() is on a buffer that was acquired and
 * never committed: it never entered the ready queue, so the consumer has never
 * seen it and returning it to the free list is a producer-local operation.
 *
 * Releasing a buffer that HAD been committed would be the bug -- two threads
 * would then own it.  That cannot arise here because s_held is cleared by the
 * same statement that commits.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "stream_sink.h"
#include "stream_session.h"
#include "../ebadge_log.h"

/* The transport core.  stdint / stdbool / stddef only -- no guidef.h, no
 * gui_obj.h, so app/protocol/ stays free of the GUI object model the way
 * xfer_session.c's def_file.h include does.  See stream_sink.h. */
#include "stream_transport.h"

/*----------------------------------------------------------------------------*
 *  Producer state  (l2_task-owned; no locking -- see the file header)
 *----------------------------------------------------------------------------*/

/** The transport, fetched lazily on first use.  Lazily rather than at
 *  stream_sink_init() because init order does not guarantee the GUI has run
 *  app_stream_transport_init() yet, and a NULL cached here would be permanent.
 *  Re-fetching until it is non-NULL costs one function call per frame. */
static stp_transport_t *s_tp;

/** Buffer borrowed for the frame currently being copied, and whether we hold
 *  it.  A separate flag rather than testing s_frame.addr: a released
 *  descriptor keeps its address, and "do I own this" is the question being
 *  asked. */
static stp_frame_t s_frame;
static bool        s_held;

/** Set when the current frame is being skipped -- no buffer was available, or
 *  it did not fit.  Distinguishes "not holding because the frame is done" from
 *  "not holding because this frame is being thrown away", which decides whether
 *  a mid-frame slice is an error worth logging. */
static bool        s_skip;

static uint32_t s_frames_shown;
static uint32_t s_frames_dropped;

/*----------------------------------------------------------------------------*
 *  The sink
 *----------------------------------------------------------------------------*/

static stp_transport_t *sink_transport(void)
{
    if (s_tp == NULL)
    {
        /* Declared locally, following ebadge_port_storage.c's notify_ui_new_resource():
         * this is one of the two protocol-side references to the designer UI layer,
         * and routing it through ViewMainFace_user.h would pull guidef.h into
         * app/protocol/ and invite the rest of the porting layer to do the same. */
        extern stp_transport_t *gui_stream_transport_get(void);
        s_tp = gui_stream_transport_get();
    }
    return s_tp;
}

/** Give back a buffer we hold but have not published.  Producer-local. */
static void release_held(void)
{
    if (s_held)
    {
        (void)stp_release(s_tp, &s_frame);
        s_held = false;
    }
}

static void on_frame_chunk(const uint8_t *chunk, uint16_t len,
                           uint32_t offset, uint32_t frame_size,
                           bool is_last, void *user)
{
    (void)user;

    stp_transport_t *tp = sink_transport();
    if (tp == NULL)
    {
        /* The GUI has not created the transport yet.  Counted, because a preview
         * that silently shows nothing is the symptom this explains. */
        if (offset == 0U)
        {
            s_frames_dropped++;
            EBADGE_WARN1("stream sink: no transport yet, frame dropped (total %u)",
                         (unsigned)s_frames_dropped);
        }
        return;
    }

    /* A new frame starts.  Anything still held belongs to a frame that never
     * completed -- on the live path that means the session was torn down between
     * two slices of the previous frame. */
    if (offset == 0U)
    {
        release_held();
        s_skip = false;

        if (!stp_acquire_free(tp, frame_size, &s_frame))
        {
            /* Two causes, both worth telling apart in the log: every buffer is
             * still with the consumer (transient, the preview catches up), or the
             * frame is larger than the biggest size class (permanent until
             * APP_STREAM_MAX_FRAME changes).  stp_acquire_free() has already
             * logged its own line with the drop counter, so this one adds the
             * frame size, which is what distinguishes the two. */
            s_skip = true;
            s_frames_dropped++;
            EBADGE_WARN2("stream sink: no buffer for %u-byte frame (dropped %u)",
                         (unsigned)frame_size, (unsigned)s_frames_dropped);
            return;
        }
        s_held = true;
    }

    if (s_skip)
    {
        return;
    }
    if (!s_held)
    {
        /* Slices arriving without a head.  Happens when a session is torn down
         * mid-frame and a later slice still reaches us; drop quietly rather than
         * writing into a buffer we do not own. */
        return;
    }

    /* Clamp against the buffer, not against frame_size: the acquire matched
     * frame_size, so capacity >= frame_size and this can only bite if a caller
     * hands out a slice past the size it declared.  Cheap, and the alternative
     * is a PSRAM overrun. */
    if (offset >= s_frame.capacity)
    {
        EBADGE_WARN2("stream sink: slice offset %u past capacity %u, dropping frame",
                     (unsigned)offset, (unsigned)s_frame.capacity);
        release_held();
        s_skip = true;
        s_frames_dropped++;
        return;
    }
    uint32_t room = s_frame.capacity - offset;
    uint32_t n    = (len > room) ? room : (uint32_t)len;
    memcpy((uint8_t *)s_frame.addr + offset, chunk, n);

    if (is_last)
    {
        /* Every JPEG frame is independently decodable, so is_keyframe is
         * unconditionally true -- which is what lets the consumer run
         * UNCONDITIONAL drop and jump to the newest frame without corrupting a
         * decoder.  (The MSV1 producer this replaced had to say false here.) */
        uint32_t used = offset + n;
        s_held = false;                  /* published: no longer ours to release */
        if (stp_commit(tp, &s_frame, used, true))
        {
            s_frames_shown++;
            if (s_frames_shown == 1U || (s_frames_shown % 30U) == 0U)
            {
                EBADGE_LOG2("stream sink: frame #%u committed, %u bytes",
                            (unsigned)s_frames_shown, (unsigned)used);
            }
        }
        else
        {
            /* stp_commit recycles the buffer itself on failure (used > capacity,
             * or a full ready queue), so there is nothing to release here. */
            s_frames_dropped++;
            EBADGE_WARN1("stream sink: commit rejected %u bytes", (unsigned)used);
        }
    }
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void stream_sink_init(void)
{
    s_tp   = NULL;
    s_held = false;
    s_skip = false;
    memset(&s_frame, 0, sizeof(s_frame));
    stream_session_set_frame_sink(on_frame_chunk, NULL);
    EBADGE_LOG("stream sink: installed (JPEG frames -> gui_stream transport)");
}

void stream_sink_reset(void)
{
    if (s_held)
    {
        EBADGE_LOG1("stream sink: reset, returning a half-filled buffer (%u B cap)",
                    (unsigned)s_frame.capacity);
    }
    release_held();
    s_skip = false;
}

uint32_t stream_sink_frames_shown(void)   { return s_frames_shown; }
uint32_t stream_sink_frames_dropped(void) { return s_frames_dropped; }
