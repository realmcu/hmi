/* ================================================================
 * FlashDB TSDB 使用示例
 *
 * 功能：open → append (write/ioctl 两种) → 时间区间游标查询
 *      → query_count → clean → 关闭
 * 编译要求：posix.h + ioctls/posix_ioctl_fdb.h, FDB_USING_TSDB
 *
 * 注意：本文件只演示 TSDB 抽象层能力。业务语义（pedo/health 等）
 *      的存储示例见 component/gsensor-algorithm/pedo_logger.c。
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_fdb.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

struct env_record
{
    int temp;
    int humi;
};

void example_fdb_ts(void)
{
    posix_fd_t fd = posix_open("/dev/fdb/ts/log");
    if (fd == POSIX_FD_NULL)
    {
        return;
    }

    struct env_record r1 = { .temp = 25, .humi = 60 };
    posix_fdb_ts_append_t a =
    {
        .buf = &r1,
        .len = sizeof(r1),
        .ts  = POSIX_FDB_TS_AUTO_TIME,
    };
    posix_ioctl(fd, POSIX_FDB_TS_IOCTL_APPEND, &a);

    struct env_record r2 = { .temp = 26, .humi = 62 };
    posix_write(fd, &r2, sizeof(r2));

    posix_fdb_ts_iter_init_t cfg =
    {
        .from    = 0,
        .to      = 0,
        .by_time = false,
        .reverse = false,
    };
    posix_ioctl(fd, POSIX_FDB_TS_IOCTL_ITER_INIT, &cfg);

    struct env_record rd;
    posix_fdb_ts_entry_t e = { .buf = &rd, .buf_len = sizeof(rd) };
    while (posix_ioctl(fd, POSIX_FDB_TS_IOCTL_ITER_NEXT, &e) == POSIX_OK)
    {
        /* e.ts / e.status / rd.temp / rd.humi 可供应用处理 */
        (void)e;
        (void)rd;
    }

    posix_fdb_ts_count_t qc =
    {
        .from   = 0,
        .to     = 0xFFFFFFFF,
        .status = 2,
    };
    posix_ioctl(fd, POSIX_FDB_TS_IOCTL_QUERY_COUNT, &qc);

    posix_close(fd);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_fdb_ts_inited = false;

static int cmd_fdb_ts(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_fdb_ts_inited) { posix_port_init_all(); s_fdb_ts_inited = true; }

    posix_fd_t fd = posix_open("/dev/fdb/ts/log");
    if (fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/fdb/ts/log failed");
        return -1;
    }

    int failures = 0;
    for (int i = 0; i < 3; i++)
    {
        struct env_record r = { .temp = 25 + i, .humi = 60 + i };
        posix_ssize_t n = posix_write(fd, &r, sizeof(r));
        shell_print(sh, "append #%d (rc=%d)", i, (int)n);
        if (n != sizeof(r))
        {
            failures++;
        }
    }

    posix_fdb_ts_iter_init_t cfg = {0};
    int rc = posix_ioctl(fd, POSIX_FDB_TS_IOCTL_ITER_INIT, &cfg);
    shell_print(sh, "iter init rc=%d", rc);

    struct env_record rd;
    posix_fdb_ts_entry_t e = { .buf = &rd, .buf_len = sizeof(rd) };
    int n = 0;
    while (posix_ioctl(fd, POSIX_FDB_TS_IOCTL_ITER_NEXT, &e) == POSIX_OK)
    {
        shell_print(sh, "  [%d] ts=%lld temp=%d humi=%d", n++,
                    (long long)e.ts, rd.temp, rd.humi);
    }

    posix_close(fd);
    if (failures != 0 || n < 3)
    {
        shell_error(sh, "POSIX FDB TS test FAILED (writes=%d records=%d)",
                    3 - failures, n);
        return -1;
    }
    shell_print(sh, "POSIX FDB TS test PASSED (%d records)", n);
    return 0;
}
SHELL_CMD_REGISTER(posix_fdb_ts, NULL, "POSIX FlashDB TS test", cmd_fdb_ts);
#endif /* CONFIG_SHELL */
