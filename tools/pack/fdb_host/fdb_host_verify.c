/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * fdb_host_verify -- read a pack.py FDB image back as "flash" and replay the
 * boot path with the firmware's own FlashDB code:
 *
 *   fdb_kvdb_init -> fdb_bf_init -> fdb_bf_foreach
 *
 * Output matches bf_boot_enum_cb() in app/example_gui_flashdb.c, so prebuilt
 * resources can be checked on the PC without flashing the board.
 *
 * Usage:
 *   fdb_host_verify --img <img.bin> --kvdb-size <bytes> --bf-size <bytes> \
 *                   [--key <expected-key> ...]
 *
 * With --key, any missing key exits non-zero (for CI).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "flashdb.h"
#include "fdb_bf.h"

static uint8_t *g_img;
static size_t   g_len;

static int host_read(long offset, uint8_t *buf, size_t size)
{
    if (offset < 0 || (size_t)offset + size > g_len) { return -1; }
    memcpy(buf, g_img + offset, size);
    return (int)size;
}

static int host_write(long offset, const uint8_t *buf, size_t size)
{
    /* Verification is read-only; a write means the init formatted the image. */
    static int warned;
    if (!warned)
    {
        fprintf(stderr, "!! write during verify @0x%lx (len=%zu)\n",
                offset, size);
        warned = 1;
    }
    if (offset < 0 || (size_t)offset + size > g_len) { return -1; }
    memcpy(g_img + offset, buf, size);
    return (int)size;
}

static int host_erase(long offset, size_t size)
{
    fprintf(stderr, "!! erase during verify @0x%lx (len=%zu) -- "
            "the image may be considered invalid and reformatted\n", offset, size);
    if (offset < 0 || (size_t)offset + size > g_len) { return -1; }
    memset(g_img + offset, 0xFF, size);
    return (int)size;
}

static int host_init(void)
{
    return 0;
}

struct fal_flash_dev nor_flash_host =
{
    .name = "norhost", .addr = 0, .len = 0, .blk_size = 4096,
    .ops = { host_init, host_read, host_write, host_erase },
    .write_gran = 1,
};

/* ---- enumeration ---- */

struct seen
{
    char keys[64][FDB_BF_KEY_MAX];
    int  n;
};

static bool enum_cb(const char *key, const struct fdb_bf_dirent *ent,
                    uint32_t xip_addr, void *arg)
{
    struct seen *s = (struct seen *)arg;
    printf("  %-10s size=%-8u cap=%-8u off=0x%06X xip=0x%08X flags=0x%08X%s\n",
           key, ent->size, ent->capacity, ent->offset, xip_addr, ent->flags,
           (ent->flags & FDB_BF_FLAG_CRC_VALID) ? " crc" : "");
    if (s->n < (int)(sizeof(s->keys) / sizeof(s->keys[0])))
    {
        snprintf(s->keys[s->n], sizeof(s->keys[0]), "%s", key);
        s->n++;
    }
    return false;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    uint32_t kvdb_size = 0, bf_size = 0;
    uint32_t xip_base = 0;
    const char *expect[64];
    int n_expect = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--img") == 0 && i + 1 < argc) { path = argv[++i]; }
        else if (strcmp(argv[i], "--kvdb-size") == 0 && i + 1 < argc)
        { kvdb_size = (uint32_t)strtoul(argv[++i], NULL, 0); }
        else if (strcmp(argv[i], "--bf-size") == 0 && i + 1 < argc)
        { bf_size = (uint32_t)strtoul(argv[++i], NULL, 0); }
        else if (strcmp(argv[i], "--xip-base") == 0 && i + 1 < argc)
        { xip_base = (uint32_t)strtoul(argv[++i], NULL, 0); }
        else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc)
        { if (n_expect < 64) { expect[n_expect++] = argv[++i]; } }
        else
        {
            fprintf(stderr, "unknown arg %s\n", argv[i]);
            return 2;
        }
    }
    if (path == NULL || kvdb_size == 0 || bf_size == 0)
    {
        fprintf(stderr, "usage: %s --img <bin> --kvdb-size <n> --bf-size <n> "
                "[--key k]...\n", argv[0]);
        return 2;
    }

    g_len = (size_t)kvdb_size + bf_size;
    FILE *f = fopen(path, "rb");
    if (f == NULL) { perror(path); return 3; }
    g_img = malloc(g_len);
    if (g_img == NULL) { fprintf(stderr, "OOM\n"); return 3; }
    /* The image may be truncated (unused 0xFF tail is not written).  This is
     * safe: BF only reads the ranges named by the directory, and unflashed
     * areas stay at 0xFF.  Treat the missing tail as 0xFF. */
    memset(g_img, 0xFF, g_len);
    size_t got = fread(g_img, 1, g_len, f);
    if (got < kvdb_size)
    {
        fprintf(stderr, "%s is shorter than the KVDB (%zu < %u)\n",
                path, got, kvdb_size);
        return 3;
    }
    if (got < g_len)
    {
        printf("note: image %zu B shorter than kvdb+bf %zu B; tail treated as 0xFF\n",
               got, g_len);
    }
    fclose(f);
    nor_flash_host.len = g_len;

    if (fal_init() <= 0) { fprintf(stderr, "fal_init failed\n"); return 3; }


    static struct fdb_kvdb kvdb;
    static struct fdb_bf bf;
    fdb_err_t rc = fdb_kvdb_init(&kvdb, "env", "fdb_kvdb1", NULL, NULL);
    if (rc != FDB_NO_ERR) { fprintf(stderr, "kvdb init rc=%d\n", (int)rc); return 4; }
    rc = fdb_bf_init(&bf, &kvdb, "bf_data", NULL);
    if (rc != FDB_NO_ERR) { fprintf(stderr, "bf init rc=%d\n", (int)rc); return 4; }

    struct fdb_bf_space sp;
    if (fdb_bf_space(&bf, &sp) == FDB_NO_ERR)
    {
        printf("bf: total=%u used=%u free=%u largest=%u files=%u blk=%u\n",
               sp.total_size, sp.used_size, sp.free_size, sp.largest_free,
               sp.file_count, sp.blk_size);
    }

    struct seen s = {0};
    printf("bf directory (what the firmware enumerates at boot):\n");
    fdb_bf_foreach(&bf, enum_cb, &s);

    /* Check each index header: every slot address must be inside its record. */
    uint32_t bf_xip_base = xip_base ? (xip_base + kvdb_size) : 0;
    if (xip_base)
    {
        struct { const char *key; uint32_t xip; size_t sz; } recs[64];
        int nr = 0;
        for (int i = 0; i < s.n && nr < 64; i++)
        {
            uint32_t a = 0; size_t z = 0;
            if (fdb_bf_get_addr(&bf, s.keys[i], &a, &z) == FDB_NO_ERR)
            { recs[nr].key = s.keys[i]; recs[nr].xip = a; recs[nr].sz = z; nr++; }
        }
        for (int i = 0; i < nr; i++)
        {
            uint32_t a = recs[i].xip; size_t z = recs[i].sz;
            if (z < 60) { printf("  !! %s size=%zu < 60\n", recs[i].key, z); return 6; }
            /* fdb_bf_get_addr returns flash.addr + part.offset + ent.offset.
             * On host flash.addr is 0, so `a` is an offset inside the image.
             * Convert it to a XIP address for the index comparison. */
            uint32_t a_xip = a + xip_base;   /* xip_base = KVDB address */
            if (a + z > g_len)
            { printf("  !! %s offset %#x out of image\n", recs[i].key, a); return 6; }
            const uint8_t *p = g_img + a;
            uint8_t id = p[40];
            const char *nm = id==0x81?"IMG":id==0x82?"VIDEO":id==0x85?"TEAR":"?";
            printf("  %-8s id=0x%02X(%s)", recs[i].key, id, nm);
            for (int f = 0; f < 4; f++)
            {
                uint32_t v = 0;
                memcpy(&v, p + 44 + f*4, 4);
                if (v == 0) { printf("  [%d]-", f); continue; }
                if (v < a_xip || v >= a_xip + z)
                {
                    printf("  [%d]=%#x OUT-OF-RECORD(%#x..%#x) !!",
                           f, v, a_xip, a_xip + z);
                    return 6;
                }
                printf("  [%d]+%#x", f, v - a_xip);
            }
            printf("\n");
        }
    }


    int missing = 0;
    for (int i = 0; i < n_expect; i++)
    {
        int found = 0;
        for (int j = 0; j < s.n; j++)
        {
            if (strcmp(s.keys[j], expect[i]) == 0) { found = 1; break; }
        }
        if (!found) { fprintf(stderr, "MISSING key: %s\n", expect[i]); missing++; }
    }
    if (missing) { return 5; }

    printf("verify OK (%d file(s)%s)\n", s.n,
           n_expect ? ", all expected keys present" : "");
    return 0;
}
