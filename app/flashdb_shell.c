/**
 * @file    flashdb_shell.c
 * @brief   `fdb` shell command -- BF (big file) space and directory listing.
 *
 * `fdb space` answers "how much room is left for another picture".  The number
 * that matters is `largest contiguous`, not `free`: fdb_bf_create() only ever
 * allocates one contiguous block-aligned run, so a partition with 1 MB free
 * split across two 512 KB holes cannot take a 600 KB file.
 */
#if defined(CONFIG_SHELL)

#include <zephyr/shell/shell.h>
#include <stdio.h>
#include <string.h>
#include "flashdb.h"

extern fdb_bf_t app_get_bf(void);

static void print_kb(const struct shell *sh, const char *label, uint32_t bytes)
{
    /* Integer-only: no float formatting in this build's printf. */
    shell_print(sh, "  %-20s %8u B  (%u.%02u MB)", label, bytes,
                bytes / (1024U * 1024U),
                (bytes % (1024U * 1024U)) * 100U / (1024U * 1024U));
}

static int cmd_space(const struct shell *sh, size_t argc, char **argv)
{
    struct fdb_bf_space sp;
    fdb_err_t rc;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    rc = fdb_bf_space(app_get_bf(), &sp);
    if (rc != FDB_NO_ERR)
    {
        shell_error(sh, "fdb_bf_space failed (%d) -- BF not initialised?", (int)rc);
        return -EIO;
    }

    shell_print(sh, "BF data partition");
    print_kb(sh, "total", sp.total_size);
    print_kb(sh, "used (allocated)", sp.used_size);
    print_kb(sh, "free (sum)", sp.free_size);
    print_kb(sh, "largest contiguous", sp.largest_free);
    print_kb(sh, "valid data", sp.valid_size);
    shell_print(sh, "  %-20s %8u", "files", sp.file_count);
    shell_print(sh, "  %-20s %8u B", "block size", sp.blk_size);

    /* used - valid is the tail waste from rounding each file up to a block. */
    if (sp.used_size > sp.valid_size)
    {
        print_kb(sh, "block-align waste", sp.used_size - sp.valid_size);
    }
    if (sp.largest_free < sp.free_size)
    {
        shell_warn(sh, "fragmented: a create() is capped at %u B, not %u B",
                   sp.largest_free, sp.free_size);
    }
    if (sp.truncated)
    {
        shell_warn(sh, "more than %d files -- 'largest contiguous' is an upper"
                   " bound only", FDB_BF_MAX_ENTRIES);
    }
    return 0;
}

static bool list_cb(const char *key, const struct fdb_bf_dirent *ent,
                    uint32_t xip_addr, void *arg)
{
    const struct shell *sh = (const struct shell *)arg;

    shell_print(sh, "  %-24s size=%-8u cap=%-8u off=0x%06X xip=0x%08X%s",
                key, ent->size, ent->capacity, ent->offset, xip_addr,
                (ent->flags & FDB_BF_FLAG_CRC_VALID) ? " crc" : "");
    return false;
}

static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "BF directory");
    fdb_bf_foreach(app_get_bf(), list_cb, (void *)sh);
    return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t removed = 0;
    fdb_err_t rc;

    /* Destructive and not undoable, so it takes an explicit confirmation word
     * rather than trusting a bare `fdb reset` typed by mistake. */
    if (argc != 2 || strcmp(argv[1], "yes") != 0)
    {
        shell_error(sh, "erases ALL big files. run: fdb reset yes");
        return -EINVAL;
    }

    shell_print(sh, "resetting BF area (erase may take a few seconds)...");

    rc = fdb_bf_reset(app_get_bf(), &removed);
    if (rc != FDB_NO_ERR)
    {
        /* removed is filled in even on failure -- report it so a partial reset
         * is visible instead of looking like nothing happened. */
        shell_error(sh, "fdb_bf_reset failed (%d), %u entries removed",
                    (int)rc, removed);
        return -EIO;
    }

    shell_print(sh, "BF reset done: %u entries removed, data partition erased",
                removed);
    shell_warn(sh, "the UI's file list still holds the old addresses -- reboot");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fdb,
                               SHELL_CMD(space, NULL, "BF partition space usage", cmd_space),
                               SHELL_CMD(list,  NULL, "list all big files", cmd_list),
                               SHELL_CMD(reset, NULL, "erase ALL big files: fdb reset yes", cmd_reset),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fdb, &sub_fdb, "FlashDB big-file diagnostics", NULL);

#endif /* CONFIG_SHELL */
