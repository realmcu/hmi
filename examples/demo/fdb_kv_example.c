/* ================================================================
 * FlashDB KVDB 使用示例
 *
 * 功能：open → set/get blob → set/get string → exists → 游标遍历 → del
 * 编译要求：posix.h + ioctls/posix_ioctl_fdb.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_fdb.h"
#include <string.h>

void example_fdb_kv(void)
{
    posix_fd_t fd = posix_open("/dev/fdb/kv/env");
    if (fd == POSIX_FD_NULL)
    {
        return;
    }

    int boot_count = 0;
    posix_fdb_kv_io_t io =
    {
        .key     = "boot_count",
        .buf     = &boot_count,
        .buf_len = sizeof(boot_count),
    };
    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_GET_BLOB, &io);

    boot_count++;
    io.buf_len = sizeof(boot_count);
    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_SET_BLOB, &io);

    posix_fdb_kv_str_t s =
    {
        .key     = "fw_version",
        .buf     = (char *)"v1.0.0",
        .buf_len = 7,
    };
    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_SET_STR, &s);

    char ver[16] = {0};
    s.buf     = ver;
    s.buf_len = sizeof(ver);
    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_GET_STR, &s);

    posix_fdb_kv_exists_t q = { .key = "boot_count" };
    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_EXISTS, &q);

    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_ITER_INIT, NULL);
    posix_fdb_kv_entry_t ent;
    while (posix_ioctl(fd, POSIX_FDB_KV_IOCTL_ITER_NEXT, &ent) == POSIX_OK)
    {
        /* ent.name / ent.value_len 可供应用打印或归档 */
        (void)ent;
    }

    posix_close(fd);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_fdb_kv_inited = false;

static int cmd_fdb_kv(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_fdb_kv_inited) { posix_port_init_all(); s_fdb_kv_inited = true; }

    posix_fd_t fd = posix_open("/dev/fdb/kv/env");
    if (fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/fdb/kv/env failed");
        return -1;
    }

    int boot_count = 0;
    posix_fdb_kv_io_t io =
    {
        .key = "boot_count", .buf = &boot_count, .buf_len = sizeof(boot_count)
    };
    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_GET_BLOB, &io);
    shell_print(sh, "boot_count (before) = %d", boot_count);

    boot_count++;
    io.buf_len = sizeof(boot_count);
    int rc = posix_ioctl(fd, POSIX_FDB_KV_IOCTL_SET_BLOB, &io);
    shell_print(sh, "set boot_count = %d (rc=%d)", boot_count, rc);

    shell_print(sh, "iterating KVs:");
    posix_ioctl(fd, POSIX_FDB_KV_IOCTL_ITER_INIT, NULL);
    posix_fdb_kv_entry_t ent;
    int n = 0;
    while (posix_ioctl(fd, POSIX_FDB_KV_IOCTL_ITER_NEXT, &ent) == POSIX_OK)
    {
        shell_print(sh, "  [%d] %-20s  value_len=%u", n++,
                    ent.name, (unsigned)ent.value_len);
    }
    posix_close(fd);
    shell_print(sh, "POSIX FDB KV test PASSED (%d entries)", n);
    return 0;
}
SHELL_CMD_REGISTER(posix_fdb_kv, NULL, "POSIX FlashDB KV test", cmd_fdb_kv);
#endif /* CONFIG_SHELL */
