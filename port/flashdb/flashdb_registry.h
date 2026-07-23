/* ================================================================
 * FlashDB Instance Registry —— eBadge 平台
 *
 * 集中声明本项目里所有 FlashDB 实例（KVDB / TSDB / BF），并提供
 * 唯一的初始化入口与句柄 getter。业务模块永远通过本文件里的
 * getter 拿 db 句柄，不自己 fdb_*_init。
 *
 * 现有实例：
 *   1) fal_init()                          ← FAL 分区表（只做一次）
 *   2) s_env_kvdb   ("env"   @ kvdb)       ← 配置/状态用 KVDB
 *   3) s_pedo_tsdb  ("pedo"  @ fdb_tsdb1)  ← 步频/健康事件用 TSDB
 *   4) s_asset_bf   (dir 在 env kvdb,
 *                    data @ bf_data)       ← 大 blob 用 BF（图片/字体等）
 *
 * 设计约束：
 *   - 单次 init：flashdb_registry_init 幂等，任意入口首次调用触发；
 *   - 句柄稳定：getter 返回值在进程生命周期内不变，可安全缓存；
 *   - 失败可查：任一子系统 init 失败时对应 getter 返回 NULL，其它 db 仍可用；
 *   - TSDB 时间源：get_time 挂到 flashdb_get_time()（fdb_time_rtc.c 提供），
 *                  统一为 UTC epoch 秒。
 * ================================================================ */

#ifndef FLASHDB_REGISTRY_H
#define FLASHDB_REGISTRY_H

#include <flashdb.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 FAL + 所有已注册的 db 实例。幂等：多次调用只有第一次生效。
 * 返回 0 成功；<0 失败（失败的 getter 后续会返回 NULL，其它 db 不受影响）。 */
int flashdb_registry_init(void);

/* 获取 KVDB 句柄（"env" 数据库）。首次调用会隐式触发 flashdb_registry_init。
 * 返回 NULL 表示初始化失败或 KVDB 未就绪。 */
fdb_kvdb_t flashdb_registry_get_env_kvdb(void);

/* 获取 TSDB 句柄（"pedo" 数据库）。首次调用会隐式触发 flashdb_registry_init。
 * 返回 NULL 表示初始化失败或 TSDB 未就绪。 */
fdb_tsdb_t flashdb_registry_get_pedo_tsdb(void);

#ifdef FDB_USING_BF
/* 获取 BF 句柄（"asset" 大文件仓）。目录寄生在 env KVDB，数据在 bf_data 分区。
 * 首次调用会隐式触发 flashdb_registry_init。返回 NULL 表示未就绪。 */
fdb_bf_t   flashdb_registry_get_asset_bf(void);
#endif

/* fdb_time_rtc.c 里定义。留在头文件里让业务代码也能直接调用（例如
 * 需要"用同一时间源打时间戳"的场景）。 */
fdb_time_t flashdb_get_time(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASHDB_REGISTRY_H */
