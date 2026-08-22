/**
 * @file    ebadge_port_storage.c
 * @brief   Storage porting layer, bound to FlashDB BigFile.
 *
 * stat() reports the live "bf_data" partition via fdb_bf_space().  The write
 * path maps one-to-one onto BF's own create/append/commit/abort:
 *
 *   wp_begin   -> fdb_bf_create   (reserves + erases the whole range)
 *   wp_write   -> fdb_bf_append
 *   wp_commit  -> fdb_bf_commit   (stores the verified CRC32)
 *   wp_abort   -> fdb_bf_abort
 *
 * Requires FDB_USING_BF (port/flashdb/fdb_cfg.h).  Turning it off is meant to
 * break the build here rather than silently resurrect made-up capacity.
 *
 * A successful commit also tells the UI, so a wallpaper that just arrived over
 * Wi-Fi shows up in the carousel without a reboot -- see notify_ui_new_resource()
 * below for why that lives here and not in xfer_session.
 */
#include <stdio.h>
#include <string.h>
#include "ebadge_port_storage.h"
#include "../ebadge_log.h"
#include "flashdb.h"

/* Owned by app/example_gui_flashdb.c, initialised in flashdb_prepare(). */
extern fdb_bf_t app_get_bf(void);

/**
 * Single, in-flight write session.  xfer_session (0x10) and cmd_send_file
 * (0x02) are mutually exclusive by protocol, so one slot is enough; a second
 * concurrent wp_begin is a bug and is rejected rather than silently queued.
 *
 * The handle exposed to callers is a small positive integer rather than the BF
 * pointer, so a stale handle from a previous session cannot be dereferenced.
 */
#define WP_HANDLE       1

static fdb_bf_file_t s_wp_file;     /* non-NULL <=> session open */
static uint16_t      s_wp_file_id;  /* id reserved at begin, returned at commit */
static uint32_t      s_wp_expect;   /* bytes promised by the offer, for logging */

/**
 * Report the real BF partition.
 *
 * `free_bytes` deliberately carries `largest_free` (the biggest *contiguous*
 * run) rather than `free_size` (the plain sum).  fdb_bf_create() only ever
 * hands out one contiguous block-aligned run, so on a fragmented partition the
 * sum overstates what can actually be stored -- and xfer_session_offer() uses
 * this very field to accept a transfer.  Reporting the sum there would accept
 * offers that then die at wp_begin, after the App had already uploaded.
 *
 * Costs one KVDB directory scan per call.  Callers are the on-demand
 * GET_STORAGE command and the once-per-transfer offer pre-check, so this is
 * not on any hot path.
 *
 * @return 0 on success; -1 bad argument; -2 BF not initialised yet.
 */
int ebadge_port_storage_stat(ebadge_storage_stat_t *out)
{
    if (!out) { return -1; }

    struct fdb_bf_space sp;
    fdb_err_t rc = fdb_bf_space(app_get_bf(), &sp);
    if (rc != FDB_NO_ERR)
    {
        /* Before flashdb_prepare() runs, db->inited is false and this is the
         * expected answer -- not a fault.  Distinguished from a bad argument so
         * the caller can answer NOT_READY instead of FAILED. */
        EBADGE_WARN1("port_storage: fdb_bf_space rc=%d (BF not ready?)", (int)rc);
        return -2;
    }

    out->total_bytes = sp.total_size;
    out->free_bytes  = sp.largest_free;
    /* Allocated capacity, not valid bytes: the block-align tail is genuinely
     * consumed and must not read as free. */
    out->wp_used_bytes = sp.used_size;
    out->wp_count      = (uint16_t)sp.file_count;

    if (sp.largest_free < sp.free_size)
    {
        EBADGE_WARN2("port_storage: fragmented, usable=%u of %u free",
                     (unsigned)sp.largest_free, (unsigned)sp.free_size);
    }
    if (sp.truncated)
    {
        /* > FDB_BF_MAX_ENTRIES files: largest_free degraded to a
         * fragmentation-blind upper bound, so an offer may be accepted and then
         * fail at create time. */
        EBADGE_WARN("port_storage: >max entries, free_bytes is an upper bound");
    }
    return 0;
}

/**
 * Pick an unused file_id, and render it as the BF key.
 *
 * The protocol's file_id and the on-flash key are deliberately the same number
 * ("wp<id>") so that a later REPLACE_ID (§4.6 TLV 0x05) can go straight to
 * fdb_bf_delete() without a directory scan to translate id -> key.  The App's
 * own file name is not used as the key: it is up to 23 utf-8 bytes of
 * user-supplied text, which would need sanitising against FDB_BF_KEY_MAX and
 * uniquifying anyway, and duplicate names are legal on the wire.
 *
 * Scans from 1 for the first free id.  With FDB_BF_MAX_ENTRIES == 32 files the
 * loop is bounded and each probe is one KV lookup, so this stays cheap; the
 * cost lands once per transfer, next to a multi-block flash erase.
 *
 * @return 0 on success, -1 if no id is free.
 */
static int pick_file_id(uint16_t *out_id, char *key, size_t key_size)
{
    /* One past the theoretical max so a full directory is distinguishable from
     * a scan that simply ran out of patience. */
    for (uint16_t id = 1; id <= (uint16_t)(FDB_BF_MAX_ENTRIES + 1); id++)
    {
        (void)snprintf(key, key_size, "wp%u", (unsigned)id);
        if (!fdb_bf_exists(app_get_bf(), key))
        {
            *out_id = id;
            return 0;
        }
    }
    return -1;
}

int ebadge_port_storage_wp_begin(const char *name, uint32_t size,
                                 uint8_t file_type)
{
    if (s_wp_file != NULL)
    {
        EBADGE_WARN("port_storage: wp_begin while a session is open");
        return -2;
    }
    if (size == 0) { return -1; }

    char     key[FDB_BF_KEY_MAX];
    uint16_t file_id = 0;
    if (pick_file_id(&file_id, key, sizeof(key)) != 0)
    {
        EBADGE_ERR("port_storage: no free file_id (directory full)");
        return -3;
    }

    /* Reserves a contiguous block-aligned run and erases all of it before
     * returning, so this is where the multi-block NOR erase happens.  FAL's
     * rtk port kicks the watchdog inside its erase loop. */
    EBADGE_LOG2("port_storage: create '%s' size=%u", key, (unsigned)size);
    fdb_bf_file_t file = NULL;
    fdb_err_t rc = fdb_bf_create(app_get_bf(), key, size, &file);
    if (rc != FDB_NO_ERR)
    {
        /* FDB_NO_SPACE here after the offer pre-check passed means the largest
         * free run shrank in between, or the partition is more fragmented than
         * `largest_free` could express (see the truncated case in stat()). */
        EBADGE_ERR2("port_storage: fdb_bf_create '%s' rc=%d", key, (int)rc);
        return -4;
    }

    s_wp_file    = file;
    s_wp_file_id = file_id;
    s_wp_expect  = size;
    EBADGE_LOG2("port_storage: wp_begin ok id=%d type=%d",
                (int)file_id, (int)file_type);
    (void)name;   /* logged by the caller; not used as the key -- see above */
    return WP_HANDLE;
}

int ebadge_port_storage_wp_write(int handle, const uint8_t *data, uint16_t len)
{
    if (handle != WP_HANDLE || s_wp_file == NULL) { return -2; }
    if (len == 0) { return 0; }
    if (data == NULL) { return -1; }

    /* The range was erased at create time, so this is a straight page-program
     * with no erase-before-write cost -- safe to call from the slot sink, which
     * holds B2W low and thus back-pressures the 8711 while we write. */
    fdb_err_t rc = fdb_bf_append(s_wp_file, data, len);
    if (rc != FDB_NO_ERR)
    {
        /* FDB_NO_SPACE means the sender exceeded the size it declared in the
         * offer; the capacity was reserved from that number. */
        EBADGE_ERR2("port_storage: fdb_bf_append len=%d rc=%d",
                    (int)len, (int)rc);
        return -3;
    }
    return 0;
}

/**
 * Tell the UI a new resource landed on flash, so the carousel picks it up
 * without a reboot.
 *
 * Why here rather than in xfer_session: the UI wants the XIP address and length
 * of the committed data, and the only thing that knows the BF key needed to look
 * those up is this file -- the protocol's file_id is deliberately not exposed as
 * a key to callers (see pick_file_id).  Doing it here also covers every commit
 * path at once, so the 0x02 BLE-only route gets the same behaviour for free when
 * it is finally wired up.
 *
 * ui_add_resource() only posts a message to the GUI server, so this does not
 * render anything on the calling thread -- it is safe from l2_task, which is
 * where wp_commit runs.  The two words must therefore outlive this function:
 * the GUI reads them when it dequeues, not when we post.  Hence static.  One
 * in-flight transfer at a time (see the WP_HANDLE note above) is what makes a
 * single slot sufficient.
 */
static uint32_t s_ui_res_info[2];

static void notify_ui_new_resource(const char *key, uint32_t content_offset)
{
    /* Declared locally: this is the one protocol-side reference to the designer
     * UI layer, and routing it through a shared header would invite the rest of
     * the porting layer to reach into the GUI too. */
    extern void ui_add_resource(uint32_t payload);

    s_ui_res_info[0] = 0;
    s_ui_res_info[1] = 0;

    size_t    data_size = 0;
    uint32_t  addr      = 0;
    fdb_err_t rc = fdb_bf_get_addr(app_get_bf(), key, &addr, &data_size);
    if (rc != FDB_NO_ERR || addr == 0 || data_size == 0)
    {
        /* The file IS committed and readable -- only the UI hand-off failed, so
         * this is a warning and not a transfer failure.  It shows up after a
         * reboot, when the list is rebuilt from the directory. */
        EBADGE_WARN2("port_storage: no addr for '%s' (rc=%d), UI not notified",
                     key, (int)rc);
        return;
    }

    /* Skip the transport framing the file was stored with.  The UI is handed a
     * pointer it dereferences as a resource header, so it has to land on the
     * resource -- not on the EBXF header in front of it.  The framing stays on
     * flash (it is part of the CRC-verified range and part of the record of what
     * arrived); only the pointer moves past it. */
    if (content_offset >= (uint32_t)data_size)
    {
        EBADGE_WARN2("port_storage: '%s' is %u B of framing and no content",
                     key, (unsigned)data_size);
        return;
    }
    s_ui_res_info[0] = addr + content_offset;
    s_ui_res_info[1] = (uint32_t)data_size - content_offset;

    EBADGE_LOG3("port_storage: -> ui_add_resource '%s' addr=0x%08x size=%u",
                key, (unsigned)s_ui_res_info[0], (unsigned)s_ui_res_info[1]);
    if (content_offset != 0U)
    {
        EBADGE_LOG2("port_storage: (skipped %u B framing, stored %u B total)",
                    (unsigned)content_offset, (unsigned)data_size);
    }

    /* The payload is a pointer to the pair, not a value -- mainface_list_add()
     * memcpy()s 8 bytes out of it. */
    ui_add_resource((uint32_t)(uintptr_t)s_ui_res_info);
}

int ebadge_port_storage_wp_commit(int handle, uint32_t data_crc,
                                  uint32_t content_offset,
                                  uint16_t *out_file_id)
{
    if (handle != WP_HANDLE || s_wp_file == NULL) { return -2; }

    uint32_t size = s_wp_file->size;
    if (size != s_wp_expect)
    {
        /* Not fatal on its own -- the caller verified the CRC over exactly
         * these bytes -- but it means the offer and the data plane disagreed,
         * which is worth seeing in a log. */
        EBADGE_WARN2("port_storage: commit size=%u but offer promised %u",
                     (unsigned)size, (unsigned)s_wp_expect);
    }

    /* Copied before the commit: fdb_bf_commit() releases the handle slot and
     * clears it, so afterwards there is no key left to look the file up by. */
    char key[FDB_BF_KEY_MAX];
    (void)snprintf(key, sizeof(key), "%s", s_wp_file->key);

    /* Passing the CRC (rather than NULL) sets FDB_BF_FLAG_CRC_VALID, so a
     * reader can re-verify the file long after this transfer is gone. */
    fdb_err_t rc = fdb_bf_commit(s_wp_file, &data_crc);
    s_wp_file = NULL;               /* BF freed the handle either way */
    if (rc != FDB_NO_ERR)
    {
        EBADGE_ERR1("port_storage: fdb_bf_commit rc=%d", (int)rc);
        return -3;
    }

    if (out_file_id) { *out_file_id = s_wp_file_id; }
    EBADGE_LOG3("port_storage: commit ok id=%d size=%u crc=0x%08x",
                (int)s_wp_file_id, (unsigned)size, (unsigned)data_crc);

    notify_ui_new_resource(key, content_offset);
    return 0;
}

int ebadge_port_storage_wp_abort(int handle)
{
    if (handle != WP_HANDLE || s_wp_file == NULL) { return 0; }

    /* No directory entry was ever written, so the reserved range just becomes
     * free again -- nothing to erase, and the id stays unused. */
    (void)fdb_bf_abort(s_wp_file);
    s_wp_file = NULL;
    EBADGE_LOG1("port_storage: wp_abort id=%d discarded", (int)s_wp_file_id);
    return 0;
}

int ebadge_port_storage_reset(uint32_t *out_removed)
{
    uint32_t removed = 0;

    if (out_removed) { *out_removed = 0; }

    /* Checked here as well as inside BF so the local session pointer and the BF
     * handle table cannot disagree: BF would reject on its own handle, but
     * s_wp_file would still look valid to wp_write afterwards. */
    if (s_wp_file != NULL)
    {
        EBADGE_WARN("port_storage: reset refused, a write session is open");
        return -2;
    }

    fdb_err_t rc = fdb_bf_reset(app_get_bf(), &removed);
    if (out_removed) { *out_removed = removed; }

    if (rc == FDB_NO_ERR)
    {
        /* No id bookkeeping to clear: pick_file_id() probes the directory each
         * time, so an empty directory already means ids restart from 1. */
        EBADGE_LOG1("port_storage: reset ok, %u files erased", (unsigned)removed);
        return 0;
    }

    if (rc == FDB_INIT_FAILED)
    {
        EBADGE_WARN("port_storage: reset before BF init");
        return -1;
    }
    if (rc == FDB_BUSY)
    {
        return -2;
    }

    /* Erase failed: the directory is empty, so the files are gone regardless;
     * only stale bytes remain on flash. */
    EBADGE_ERR2("port_storage: reset erase failed rc=%d after %u removed",
                (int)rc, (unsigned)removed);
    return -3;
}
