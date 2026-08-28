/**
 * @file    jpgs_ingress.h
 * @brief   Preview-frame boundary detection for the raw TCP stream.
 *
 * The phone's TCP connection terminates on the 8711, not here, but the 8711 is a
 * network card rather than a protocol peer: the preview port hands us exactly the
 * bytes the phone wrote, unframed.  This module is what finds the frames in them
 * -- FF D8 to FF D9 -- buffers each one, and calls
 * stream_session_on_frame_chunk() when it is whole.
 *
 * It used to reassemble JPGS slots and route the payload to whichever session was
 * live.  Both halves are gone: the slots no longer exist, and the port already
 * answers the routing question (preview bytes belong to stream_session; a file
 * arrives on the other port and never reaches here).  Read the file header of
 * jpgs_ingress.c for what that removed and what it added -- in particular the
 * whole-frame buffer, which the old design deliberately avoided and this one
 * cannot, because a frame's length is unknowable until its EOI arrives.
 *
 * The one known weakness is an EXIF thumbnail's inner FF D9 ending the frame
 * early.  It degrades to a dropped frame; the .c header says why it is not
 * defended against here.
 */
#ifndef _EBADGE_JPGS_INGRESS_H_
#define _EBADGE_JPGS_INGRESS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Zero the scanner.  Call once from ebadge_task_init().
 *
 * Registers nothing -- stream_session is what registers with port_tcp, and this
 * module sits behind it.  The call exists so the wiring list in ebadge_task_init()
 * still names this module, and so the counters are provably zero before the first
 * byte can arrive.
 */
void jpgs_ingress_init(void);

/**
 * @brief  Raw bytes from the preview TCP port.
 *
 * Called by stream_session, which owns the port_tcp registration and has already
 * marshalled the bytes onto l2_task.  Arbitrary lengths and arbitrary boundaries
 * are expected: a marker may be split across two calls, and a call may contain the
 * end of one frame and the start of the next.
 */
void jpgs_ingress_on_tcp_data(const uint8_t *data, uint16_t len);

/**
 * @brief  Drop any frame being assembled and resync on the next SOI.
 *
 * For use when a session ends abruptly (BLE disconnect, abort): the phone may be
 * mid-frame, and without this the leftover bytes would be prepended to whatever
 * the next session receives.
 */
void jpgs_ingress_reset(void);

/** Preview-port bytes seen since boot.  Bring-up instrument: non-zero proves the
 *  phone is sending and the 8711 is forwarding, independently of whether any
 *  frame is well-formed.
 *
 *  Named _slots for source compatibility with the JPGS-slot version this
 *  replaced; there are no slots any more.  The callers are log lines, so renaming
 *  it buys nothing and costs a sweep. */
uint32_t jpgs_ingress_slots(void);

/** Frames delimited FF D8..FF D9 and handed to stream_session since boot. */
uint32_t jpgs_ingress_frames_ok(void);

/** Frames abandoned: a new SOI before the previous EOI, or a frame larger than
 *  the reassembly buffer.  Read alongside frames_ok -- a healthy preview drops
 *  none, and a steady trickle here is the EXIF-thumbnail case in the .c header. */
uint32_t jpgs_ingress_frames_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_JPGS_INGRESS_H_ */
