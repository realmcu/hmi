/**
 * @file    ebadge_port_storage.h
 * @brief   Storage porting abstraction for received files.
 *
 * begin/write/commit/abort surface matches FlashDB BigFile: a file is
 * addressed by name, written in append-only chunks, committed atomically.
 * The app-side plan is to bind this to FlashDB (see [[ebadge-flashdb-bigfile]]).
 */
#ifndef _EBADGE_PORT_STORAGE_H_
#define _EBADGE_PORT_STORAGE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Storage headline for 0x1A STORAGE_INFO / the offer pre-check.
 *
 * Units are BYTES (uint64), matching spec §4.14 where TLV_STOR_TOTAL /
 * _FREE / _WP_USED are all 8-byte LE byte counts.  An earlier revision
 * used uint32 KB here, which both narrowed the range and mismatched the
 * wire format.
 */
typedef struct
{
    uint64_t total_bytes;      /* user-writable partition total              */
    uint64_t free_bytes;       /* largest storable file -- see note below    */
    uint64_t wp_used_bytes;    /* consumed by stored wallpapers (approx ok)  */
    uint16_t wp_count;         /* wallpapers currently stored               */
} ebadge_storage_stat_t;

/**
 * Query storage headline for the STORAGE_INFO command / offer pre-check.
 *
 * NOTE on `free_bytes`: it is the largest *contiguous* free run, not the sum
 * of all free bytes.  The FlashDB BigFile allocator only hands out one
 * contiguous block-aligned run, so on a fragmented partition the sum would
 * promise space that no single file can use.  Treat this field as "the biggest
 * file that can still be stored", which is what both callers actually need.
 *
 * @return 0 on success; -1 @p out is NULL; -2 storage backend not ready yet
 *         (BF uninitialised) -- the caller should answer NOT_READY, not FAILED.
 */
int  ebadge_port_storage_stat(ebadge_storage_stat_t *out);

/**
 * @brief  Open a write session for a fresh file.
 *
 * Reserves and erases the whole @p size range up front, so this blocks for one
 * NOR erase per 4KB block (~15 blocks for a 60KB wallpaper).  Call it from the
 * session state machine with no slot in flight, never from a data callback.
 *
 * @param  name       Nul-terminated file name (utf-8, <=EB_MAX_FILE_NAME).
 *                    Informational only: the on-flash key is derived from the
 *                    assigned file_id, so the App's name is logged, not stored.
 * @param  size       Expected total bytes.
 * @param  file_type  EB_FILE_TYPE_* value.
 * @return >0 opaque write handle; <0 on error.
 */
int  ebadge_port_storage_wp_begin(const char *name, uint32_t size,
                                  uint8_t file_type);

/** Append @p len bytes at the current cursor. */
int  ebadge_port_storage_wp_write(int handle,
                                  const uint8_t *data, uint16_t len);

/**
 * @brief  Commit the file atomically (one KV set).
 *
 * Call this ONLY after the received data has passed CRC verification: commit is
 * the point where the file becomes visible, and there is no rollback afterwards.
 *
 * @param  data_crc      CRC32 of the file CONTENT -- the stored range from
 *                       @p content_offset onwards, not the framing in front of
 *                       it.  Stored in the directory entry so a later reader can
 *                       re-verify without the transfer being present, and this
 *                       is the useful one to store because it is exactly the
 *                       value the sender promised and the caller checked.
 * @param  content_offset  Bytes at the start of the stored file that are
 *                       transport framing rather than resource content, and so
 *                       must be skipped both by whatever consumes the resource
 *                       and by anyone re-checking @p data_crc.  0 when the
 *                       stored bytes are pure content.  Only affects the address
 *                       reported to the UI; the stored file is untouched.
 * @param  out_file_id   Receives the assigned file_id (>=1).
 * @return 0 on success; <0 on error.
 */
int  ebadge_port_storage_wp_commit(int handle, uint32_t data_crc,
                                   uint32_t content_offset,
                                   uint16_t *out_file_id);

/** Discard a write session (mid-transfer failure / abort). */
int  ebadge_port_storage_wp_abort(int handle);

/**
 * @brief  Erase every stored file and reset the storage records.
 *
 * Clears the whole user-writable area: every file directory entry is dropped,
 * the payload partition is erased, and all derived figures reported by
 * ebadge_port_storage_stat() (wp_count, wp_used_bytes, free_bytes) fall back to
 * empty.  There is no undo.
 *
 * Blocks for the full partition erase -- seconds, not milliseconds -- so call it
 * from a task context that may stall, never from a data callback or an ISR.
 * Rejected while a write session is open; abort the transfer first.
 *
 * The caller is responsible for whatever holds file addresses at the app level
 * (the UI's wallpaper list is built once at boot from the directory and is NOT
 * refreshed by this call).
 *
 * @param  out_removed  Optional; receives the number of files erased.  It is
 *                      filled in even when the call fails partway.
 * @return 0 on success; -1 storage backend not ready; -2 a write session is
 *         open; -3 the payload erase failed (the files are already gone).
 */
int  ebadge_port_storage_reset(uint32_t *out_removed);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_STORAGE_H_ */
