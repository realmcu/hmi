/**
 * @file    ebadge_port_storage.c
 * @brief   Storage porting stub -- to be bound to FlashDB BigFile.
 *
 * TODO(port): every function below must be re-implemented against
 * flashdb_test.c / fdb_bf.h once the app is ready.  The plan (per user):
 *   wp_begin   -> fdb_bf_start   with size + kv-meta for name/type
 *   wp_write   -> fdb_bf_append
 *   wp_commit  -> fdb_bf_end     (returns assigned bf_id / file_id)
 *   wp_abort   -> fdb_bf_abort
 *
 * Until then the stub returns synthetic success so BLE-side plumbing stays
 * exercisable without a working data plane.
 */
#include "ebadge_port_storage.h"
#include "../ebadge_log.h"

/* Single, in-flight write session -- xfer_session guarantees serial use. */
static bool s_wp_open;

int ebadge_port_storage_stat(ebadge_storage_stat_t *out)
{
    if (!out) { return -1; }
    /* TODO(port): query the underlying storage backend.  Synthetic values
     * below let GET_STORAGE respond with plausible defaults during bring-up.
     * Units are BYTES per spec §4.14 -- 4MiB total / 3MiB free.            */
    out->total_bytes   = 4u * 1024u * 1024u;
    out->free_bytes    = 3u * 1024u * 1024u;
    out->wp_used_bytes = 0;
    out->wp_count      = 0;
    return 0;
}

int ebadge_port_storage_wp_begin(const char *name, uint32_t size,
                                 uint8_t file_type)
{
    (void)name; (void)size; (void)file_type;
    if (s_wp_open) { return -2; }
    s_wp_open = true;
    EBADGE_LOG2("port_storage: wp_begin STUB size=%u type=%d",
                (unsigned)size, (int)file_type);
    /* TODO(port): call fdb_bf_start; return the handle it hands back.     */
    return 1;   /* opaque nonzero handle */
}

int ebadge_port_storage_wp_write(int handle, const uint8_t *data, uint16_t len)
{
    (void)handle; (void)data; (void)len;
    /* TODO(port): fdb_bf_append(handle, data, len).                       */
    return 0;
}

int ebadge_port_storage_wp_commit(int handle, uint16_t *out_file_id)
{
    (void)handle;
    if (!s_wp_open) { return -2; }
    s_wp_open = false;
    if (out_file_id) { *out_file_id = 1; }  /* TODO(port): real file_id.  */
    EBADGE_LOG("port_storage: wp_commit STUB");
    return 0;
}

int ebadge_port_storage_wp_abort(int handle)
{
    (void)handle;
    if (!s_wp_open) { return 0; }
    s_wp_open = false;
    EBADGE_LOG("port_storage: wp_abort STUB");
    /* TODO(port): fdb_bf_abort(handle).                                   */
    return 0;
}
