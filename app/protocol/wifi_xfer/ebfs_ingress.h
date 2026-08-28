/**
 * @file    ebfs_ingress.h
 * @brief   File-upload path from the file TCP port to xfer_session.
 *
 * The sibling of jpgs_ingress.h, and the asymmetry between them is now the whole
 * story.  Both take raw TCP bytes off one port, and:
 *
 *   the PREVIEW port  has to find frame boundaries, because nothing in the stream
 *                     declares them -- so jpgs_ingress buffers a frame and scans
 *                     for FF D8 / FF D9.
 *   the FILE port     needs no framing at all: the phone's 40-byte EBXF header is
 *                     the first thing on the connection and it declares the size,
 *                     so xfer_session's own accumulator handles everything.
 *
 * Which is why this file is thin and its sibling is not.  What remains here is a
 * state guard, a pass-through to xfer_session_on_tcp_data(), and the counters that
 * answer "is anything arriving?" independently of whether the session likes it.
 *
 * Both modules used to be slot reassemblers -- 64-byte EBFS headers here, 32-byte
 * JPGS ones there, dispatched by magic in the custom firmware's SPI layer.  The
 * standard firmware has no slots.  Read the .c header for what that deleted; the
 * short version is that every field those headers carried described the framing
 * layer rather than the file.
 */
#ifndef _EBADGE_EBFS_INGRESS_H_
#define _EBADGE_EBFS_INGRESS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Zero the counters.  Call once at startup.
 *
 * Registers nothing -- xfer_session owns the port_tcp registration and this
 * module sits behind it.
 */
void ebfs_ingress_init(void);

/**
 * @brief  Raw bytes from the file TCP port.
 *
 * Called by xfer_session, which has already marshalled them onto l2_task.  Passed
 * to xfer_session_on_tcp_data() UNSTRIPPED: the 40-byte EBXF header is still in
 * front of the file and that function is the one that knows it.
 */
void ebfs_ingress_on_tcp_data(const uint8_t *data, uint16_t len);

/**
 * @brief  Forget any upload in progress.
 *
 * Called when a session ends for any reason.  Only the counters care -- there is
 * no reassembly state left to corrupt -- but a file abandoned without this would
 * be counted neither ok nor dropped.
 */
void ebfs_ingress_reset(void);

/** Bytes seen on the file port since boot.  Named _slots for source
 *  compatibility with the version this replaced; there are no slots any more. */
uint32_t ebfs_ingress_slots(void);
uint32_t ebfs_ingress_files_ok(void);
uint32_t ebfs_ingress_files_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_EBFS_INGRESS_H_ */
