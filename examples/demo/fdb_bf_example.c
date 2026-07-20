/* ================================================================
 * FlashDB BF (Big File) 使用示例
 *
 * 功能：open → create → posix_write 多次 append → commit
 *      → select_read → posix_read 顺序读 → get_xip → foreach
 *      → delete → close
 * 编译要求：posix.h + ioctls/posix_ioctl_fdb.h, FDB_USING_BF
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_fdb.h"
#include <string.h>

void example_fdb_bf(void)
{
    posix_fd_t fd = posix_open("/dev/fdb/bf/firmware");
    if (fd == POSIX_FD_NULL)
    {
        return;
    }

    static const char part1[] = "Hello from POSIX FlashDB BF. ";
    static const char part2[] = "Two writes, one file.";
    size_t total = sizeof(part1) - 1 + sizeof(part2) - 1;

    posix_fdb_bf_create_t c = { .key = "demo/hello", .max_size = total };
    if (posix_ioctl(fd, POSIX_FDB_BF_IOCTL_CREATE, &c) != POSIX_OK)
    {
        posix_close(fd);
        return;
    }

    posix_write(fd, part1, sizeof(part1) - 1);
    posix_write(fd, part2, sizeof(part2) - 1);
    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_COMMIT, NULL);

    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_SELECT_READ, (void *)"demo/hello");
    char buf[64] = {0};
    posix_ssize_t got = posix_read(fd, buf, sizeof(buf) - 1);
    (void)got;

    posix_fdb_bf_xip_t x = { .key = "demo/hello" };
    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_GET_XIP, &x);
    /* x.xip_addr / x.size 可直接喂给 DMA 控制器 */

    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_FOREACH_INIT, NULL);
    posix_fdb_bf_entry_t e;
    while (posix_ioctl(fd, POSIX_FDB_BF_IOCTL_FOREACH_NEXT, &e) == POSIX_OK)
    {
        (void)e;
    }

    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_DELETE, (void *)"demo/hello");
    posix_close(fd);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_fdb_bf_inited = false;

static int cmd_fdb_bf(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_fdb_bf_inited) { posix_port_init_all(); s_fdb_bf_inited = true; }

    posix_fd_t fd = posix_open("/dev/fdb/bf/firmware");
    if (fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/fdb/bf/firmware failed");
        return -1;
    }

    static const char payload[] =
        "POSIX FlashDB Big File demo payload. "
        "Stored in dedicated NOR data partition, XIP-readable.";
    size_t plen = sizeof(payload) - 1;

    posix_fdb_bf_create_t c = { .key = "demo/hello", .max_size = plen };
    int rc = posix_ioctl(fd, POSIX_FDB_BF_IOCTL_CREATE, &c);
    shell_print(sh, "create: rc=%d", rc);
    if (rc != POSIX_OK)
    {
        posix_close(fd);
        return -1;
    }

    posix_ssize_t wn = posix_write(fd, payload, plen);
    shell_print(sh, "write: %d bytes", (int)wn);
    if (wn != (posix_ssize_t)plen)
    {
        posix_ioctl(fd, POSIX_FDB_BF_IOCTL_ABORT, NULL);
        posix_close(fd);
        return -1;
    }

    rc = posix_ioctl(fd, POSIX_FDB_BF_IOCTL_COMMIT, NULL);
    shell_print(sh, "commit: rc=%d", rc);
    if (rc != POSIX_OK)
    {
        posix_close(fd);
        return -1;
    }

    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_SELECT_READ, (void *)"demo/hello");
    char buf[128] = {0};
    posix_ssize_t got = posix_read(fd, buf, sizeof(buf) - 1);
    shell_print(sh, "read: %d bytes -> '%s'", (int)got, buf);

    posix_fdb_bf_xip_t x = { .key = "demo/hello" };
    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_GET_XIP, &x);
    shell_print(sh, "xip: addr=0x%08X size=%u",
                (unsigned)x.xip_addr, (unsigned)x.size);

    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_FOREACH_INIT, NULL);
    posix_fdb_bf_entry_t e;
    int n = 0;
    while (posix_ioctl(fd, POSIX_FDB_BF_IOCTL_FOREACH_NEXT, &e) == POSIX_OK)
    {
        shell_print(sh, "  [%d] %-20s size=%u xip=0x%08X",
                    n++, e.key, (unsigned)e.ent.size, (unsigned)e.xip_addr);
    }

    posix_ioctl(fd, POSIX_FDB_BF_IOCTL_DELETE, (void *)"demo/hello");
    posix_close(fd);
    shell_print(sh, "POSIX FDB BF test PASSED");
    return 0;
}
SHELL_CMD_REGISTER(posix_fdb_bf, NULL, "POSIX FlashDB BF test", cmd_fdb_bf);
#endif /* CONFIG_SHELL */
