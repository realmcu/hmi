/* ================================================================
 * FlashDB Instance Registry —— eBadge platform
 *
 * Central declaration of every FlashDB instance in this project (KVDB / TSDB / BF), plus
 * the single initialization entry and the handle getters. Business modules always obtain
 * db handles through the getters in this file and never call fdb_*_init themselves.
 *
 * Existing instances:
 *   1) fal_init()                          ← FAL partition table (done only once)
 *   2) s_env_kvdb   ("env"   @ kvdb)       ← KVDB for config/state
 *   3) s_pedo_tsdb  ("pedo"  @ fdb_tsdb1)  ← TSDB for step/health events
 *   4) s_asset_bf   (dir in env kvdb,
 *                    data @ bf_data)       ← BF for large blobs (images/fonts etc.)
 *
 * Design constraints:
 *   - single init: flashdb_registry_init is idempotent, triggered by the first call from any entry;
 *   - stable handles: getter return values never change during the process lifetime, safe to cache;
 *   - observable failures: if a subsystem init fails its getter returns NULL, the other dbs still work;
 *   - TSDB time source: get_time is hooked to flashdb_get_time() (provided by fdb_time_rtc.c),
 *                  uniformly UTC epoch seconds.
 * ================================================================ */

#ifndef FLASHDB_REGISTRY_H
#define FLASHDB_REGISTRY_H

#include <flashdb.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize FAL + all registered db instances. Idempotent: only the first call has effect.
 * Returns 0 on success; <0 on failure (the failing getter then returns NULL, other dbs are unaffected). */
int flashdb_registry_init(void);

/* Get the KVDB handle (the "env" database). The first call implicitly triggers flashdb_registry_init.
 * Returns NULL if initialization failed or the KVDB is not ready. */
fdb_kvdb_t flashdb_registry_get_env_kvdb(void);

/* Get the TSDB handle (the "pedo" database). The first call implicitly triggers flashdb_registry_init.
 * Returns NULL if initialization failed or the TSDB is not ready. */
fdb_tsdb_t flashdb_registry_get_pedo_tsdb(void);

#ifdef FDB_USING_BF
/* Get the BF handle (the "asset" large-file store). Its directory lives in the env KVDB, the data in the bf_data partition.
 * The first call implicitly triggers flashdb_registry_init. Returns NULL if not ready. */
fdb_bf_t   flashdb_registry_get_asset_bf(void);
#endif

/* Defined in fdb_time_rtc.c. Kept in the header so business code can call it directly (for
 * cases that need "timestamp with the same time source"). */
fdb_time_t flashdb_get_time(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASHDB_REGISTRY_H */
