/**
 * @file    ebfs_ingress.h
 * @brief   EBFS file-slot ingress: the inbound half of the port-9000 upload.
 *
 * The sibling of jpgs_ingress.h.  Both take fixed 4096-byte slots off the SPI
 * link and hand payload to a session, and they exist separately because the two
 * slot types answer different questions:
 *
 *   JPGS (32 B header)  a preview *frame*.  Carries frame geometry only, so the
 *                       file's identity has to come from somewhere else.
 *   EBFS (64 B header)  a file *chunk*.  Restates the whole identity the phone
 *                       put in its EBXF header -- session, name, type, total
 *                       size and the whole-file CRC32 -- in every single slot.
 *
 * That redundancy is what this module exists to use.  Because the identity is on
 * every chunk, the header cross-check against the BLE offer (spec §5.2 rule 3)
 * can happen on the FIRST slot, before a single byte reaches flash, instead of
 * after a 2 MiB file has been written and its CRC found wanting.  A mismatch is
 * therefore cheap to act on: cut the stream with AT+XFERSTOP and tell the App
 * over BLE.
 *
 * Registered as the EBFS sink at init, alongside the JPGS one.  Both are
 * dispatched by the magic at offset 0 of the slot, in wifi_8711_at.c.
 */
#ifndef _EBADGE_EBFS_INGRESS_H_
#define _EBADGE_EBFS_INGRESS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Register the EBFS slot sink with the transport.  Call once at startup. */
void ebfs_ingress_init(void);

/**
 * @brief  Forget any file in progress.
 *
 * Called when a session ends for any reason.  Without it, leftover chunk
 * bookkeeping measured against a file this session never started would make the
 * NEXT transfer's first slot look out of order.
 */
void ebfs_ingress_reset(void);

/** Lifetime counters, for "is the upload path doing anything at all?". */
uint32_t ebfs_ingress_slots(void);
uint32_t ebfs_ingress_files_ok(void);
uint32_t ebfs_ingress_files_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_EBFS_INGRESS_H_ */
