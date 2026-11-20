/* ================================================================
 * FlashDB Instance Registry -- eBadge platform
 *
 * Declares all FlashDB instances in this project (KVDB / TSDB / BF) in one place and provides
 * the single initialization entry and handle getters. Business modules always obtain db
 * handles through the getters in this file and never call fdb_*_init themselves.
 *
 * Current instances:
 *   1) fal_init()                          <- FAL partition table (done once)
 *   2) s_env_kvdb   ("env"   @ kvdb)       <- KVDB for config/state
 *   3) s_pedo_tsdb  ("pedo"  @ fdb_tsdb1)  <- TSDB for step-rate/health events
 *   4) s_asset_bf   (dir in env kvdb,
 *                    data @ bf_data)       <- BF for large blobs (images/fonts etc.)
 *
 * Design constraints:
 *   - Single init: flashdb_registry_init is idempotent and triggered by the first call from any entry;
 *   - Stable handles: getter return values never change during the process lifetime and can be cached safely;
 *   - Failures queryable: if any subsystem init fails, its getter returns NULL and other dbs remain usable;
 *   - TSDB time source: get_time is hooked to flashdb_get_time() (provided by fdb_time_rtc.c),
 *                  uniformly UTC epoch seconds.
 * ================================================================ */

#ifndef FLASHDB_REGISTRY_H
#define FLASHDB_REGISTRY_H

#include <flashdb.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize FAL plus all registered db instances. Idempotent: only the first call takes effect.
 * Return 0 on success; <0 on failure (the failing getter then returns NULL; other dbs are unaffected). */
int flashdb_registry_init(void);

/* Get the KVDB handle (the "env" database). The first call implicitly triggers flashdb_registry_init.
 * Returns NULL if initialization failed or the KVDB is not ready. */
fdb_kvdb_t flashdb_registry_get_env_kvdb(void);

/* Get the TSDB handle (the "pedo" database). The first call implicitly triggers flashdb_registry_init.
 * Returns NULL if initialization failed or the TSDB is not ready. */
fdb_tsdb_t flashdb_registry_get_pedo_tsdb(void);

#ifdef FDB_USING_BF
/* Get the BF handle (the "asset" big-file store). The directory lives in env KVDB, the data in the bf_data partition.
 * The first call implicitly triggers flashdb_registry_init. Returns NULL if not ready. */
fdb_bf_t   flashdb_registry_get_asset_bf(void);
#endif

/* Defined in fdb_time_rtc.c. Declared in the header so business code can call it directly (e.g.
 * cases that need to "timestamp with the same time source"). */
fdb_time_t flashdb_get_time(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASHDB_REGISTRY_H */
