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
    uint64_t free_bytes;       /* currently free                            */
    uint64_t wp_used_bytes;    /* consumed by stored wallpapers (approx ok)  */
    uint16_t wp_count;         /* wallpapers currently stored               */
} ebadge_storage_stat_t;

/** Query storage headline for the STORAGE_INFO command / offer pre-check. */
int  ebadge_port_storage_stat(ebadge_storage_stat_t *out);

/**
 * @brief  Open a write session for a fresh file.
 * @param  name       Nul-terminated file name (utf-8, <=EB_MAX_FILE_NAME).
 * @param  size       Expected total bytes.
 * @param  file_type  EB_FILE_TYPE_* value.
 * @return >0 opaque write handle; <0 on error.
 */
int  ebadge_port_storage_wp_begin(const char *name, uint32_t size,
                                  uint8_t file_type);

/** Append @p len bytes at the current cursor. */
int  ebadge_port_storage_wp_write(int handle,
                                  const uint8_t *data, uint16_t len);

/** Commit the file atomically.  Returns the assigned file_id (>=1) or <0. */
int  ebadge_port_storage_wp_commit(int handle, uint16_t *out_file_id);

/** Discard a write session (mid-transfer failure / abort). */
int  ebadge_port_storage_wp_abort(int handle);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_STORAGE_H_ */
