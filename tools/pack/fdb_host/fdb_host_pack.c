/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * fdb_host_pack -- build a FlashDB (KVDB + BF) flash image on the PC.
 *
 * Why not assemble the FDB binary in Python: KVDB sector headers are bit
 * status tables (0xFF -> 0x7F -> 0x3F ...), KV headers carry CRCs, and GC /
 * record versions are implicit.  BF dirents are KV values whose offset is
 * relative to bf_data and whose capacity is block aligned.  Hand-copying any
 * of this silently produces dirty data that looks like a corrupt database.
 *
 * This tool links the firmware's own FlashDB sources (component/FlashDB) and
 * only replaces the FAL flash backend with file I/O, so the output is
 * byte-identical to what fdb_kvdb_init/fdb_bf_create/commit write on device.
 *
 * Usage:
 *   fdb_host_pack --out <img.bin> --kvdb-size <bytes> --bf-size <bytes> \
 *                  [--crc-skip <bytes>] --file <key>=<path> [--file ...]
 *
 * The image covers [kvdb, kvdb+kvdb_size+bf_size); flash it with -A set to
 * the KVDB absolute address.  Unused space stays 0xFF (erased).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "flashdb.h"
#include "fdb_bf.h"

/* ------------------------------------------------------------------ */
/* Host FAL backend: map the whole "flash device" to memory. */
/* ------------------------------------------------------------------ */

static FILE   *g_fp;
static uint8_t *g_img;      /* full image; read/write in memory, save at end */
static size_t  g_len;

/* Erased NOR flash reads as 0xFF. */
static int host_read(long offset, uint8_t *buf, size_t size)
{
    if (offset < 0 || (size_t)offset + size > g_len) { return -1; }
    memcpy(buf, g_img + offset, size);
    return (int)size;
}

static int host_write(long offset, const uint8_t *buf, size_t size)
{
    if (offset < 0 || (size_t)offset + size > g_len) { return -1; }
    /* Do not emulate NOR's 1->0-only rule: FDB always erases before writing
     * and the image is generated once.  Emulating it would false-positive
     * when format_sector rewrites a sector header. */
    memcpy(g_img + offset, buf, size);
    return (int)size;
}

static int host_erase(long offset, size_t size)
{
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
    .name       = "norhost",
    .addr       = 0,
    .len        = 0,            /* set at runtime */
    .blk_size   = 4096,
    .ops        = { host_init, host_read, host_write, host_erase },
    .write_gran = 1,
};

/* ------------------------------------------------------------------ */
/* BF dirent CRC */
/* ------------------------------------------------------------------ */

/* Match the device: xfer_session CRCs the resource body, skipping the EBXF
 * (40 B) and GUI header (16 B).  Prebuilt wallpapers use the same range so a
 * future reader-side CRC check has identical semantics. */
static uint32_t crc_from(const uint8_t *p, size_t len, size_t skip)
{
    if (skip >= len) { return 0; }
    return fdb_calc_crc32(0, p + skip, len - skip);
}

/* ------------------------------------------------------------------ */
/* Resource wrapping */
/* ------------------------------------------------------------------ */

/*
 * Firmware reads wallpapers from BF like this (ViewMainFace_user.c):
 *
 *     data = raw + 40;                        // skip EBXF
 *     gui_img_create_from_mem(..., data, ...) // must be a GUI header
 *
 * So BF stores the same blob the upload path (xfer_session.c) writes, not a
 * bare image:
 *
 *   [0..40)   EBXF header (magic/version/type/name/size/crc32)
 *   [40..56)  gui_rgb_data_head_t(8) + size(4) + pad(4)
 *   [56..)    image body (JPEG/PNG/...)
 *
 * A bare image would put raw+40 inside the data, so type/w/h are garbage.
 */

#define EBXF_HDR_LEN     40
#define GUI_IMG_HDR_LEN  16

/*
 * Resource index header: the 40 bytes right after EBXF.  Firmware reads it
 * directly instead of pairing by key.
 *
 *   0..3   id          0x81=SRC_IMG 0x82=SRC_VIDEO 0x85=SRC_TEARBADGE
 *   4..7   data        data GUI header (VIDEO: raw video body)
 *   8..11  preview     preview GUI header (0 = absent)
 *   12..15 tear_mask   param0 (tear only)
 *   16..19 tear_part   param1 (tear only)
 *   20..23 data_len    bytes of data (0 = absent)
 *   24..27 preview_len preview bytes
 *   28..31 mask_len    tear_mask bytes
 *   32..35 part_len    tear_part bytes
 *   36..39 reserved    0
 *
 * Lengths are the real slot sizes, excluding alignment padding.  Older
 * records (24-byte index) have 0 there and fall back to neighbour/record-end
 * inference.
 *
 * For VIDEO/TEAR the EBXF size/crc may be empty; only id and addresses are
 * meaningful.  VIDEO data points at the raw video body (no GUI header).
 *
 * Addresses are XIP absolute and depend on the BF layout, so packing writes
 * data + dirents first, then fills the index once addresses are known.
 */
#define RES_IDX_LEN          40
#define RES_IDX_RSVD         36

#define RES_IDX_DATA         4
#define RES_IDX_PREVIEW      8
#define RES_IDX_MASK         12
#define RES_IDX_PART         16
#define RES_IDX_DATA_LEN     20
#define RES_IDX_PREVIEW_LEN  24
#define RES_IDX_MASK_LEN     28
#define RES_IDX_PART_LEN     32

/* Slot addresses are 8-byte aligned (the GUI headers are read as 32/64-bit).
 * The 40-byte index puts the first slot at 40+40=80; pad between slots when
 * a length is not a multiple of 8. */
#define RES_ALIGN        8u

/* Tear records reserve a fixed preview slot so the device can rewrite it in
 * place.  It must own whole 4K erase blocks and never grow. */
#define TEAR_PREVIEW_CAP (80u * 1024u)

#define RES_ID_IMG       0x81
#define RES_ID_VIDEO     0x82
#define RES_ID_TEAR      0x85

#define EB_FILE_TYPE_JPEG   0x01
#define EB_FILE_TYPE_PNG    0x02
#define EB_FILE_TYPE_GIF    0x03
#define EB_FILE_TYPE_BIN    0x06

/* GUI_FormatType, from realgui/engine/draw/draw_img.h */
#define GUI_FMT_JPEG   12
#define GUI_FMT_PNG    13
#define GUI_FMT_GIF    14
#define GUI_FMT_BMP    11

struct gui_img_hdr
{
    uint8_t  flags;      /* scan:1 align:1 resize:2 compress:1 jpeg:1 idu:1 png:1 */
    uint8_t  type;       /* GUI_FormatType                                            */
    int16_t  w;
    int16_t  h;
    uint8_t  version;
    uint8_t  rsvd;
    uint32_t size;       /* image body length */
    uint32_t dummy;
} __attribute__((packed));

static uint32_t crc32_ieee(uint32_t crc, const uint8_t *p, size_t n)
{
    crc ^= 0xFFFFFFFFu;
    while (n--)
    {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1) - 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Sniff file type; returns EBXF file_type and GUI type, BIN if unknown. */
static void sniff(const uint8_t *b, size_t n, uint8_t *eb_type, uint8_t *gui_type)
{
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8)
    {
        *eb_type = EB_FILE_TYPE_JPEG; *gui_type = GUI_FMT_JPEG;
    }
    else if (n >= 8 && memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0)
    {
        *eb_type = EB_FILE_TYPE_PNG; *gui_type = GUI_FMT_PNG;
    }
    else if (n >= 6 && (memcmp(b, "GIF87a", 6) == 0 || memcmp(b, "GIF89a", 6) == 0))
    {
        *eb_type = EB_FILE_TYPE_GIF; *gui_type = GUI_FMT_GIF;
    }
    else
    {
        /* Already a GUI-wrapped .bin, or unknown: store as-is. */
        *eb_type = EB_FILE_TYPE_BIN; *gui_type = 0xFF;
    }
}

/*
 * Check whether raw already wraps a GUI header (designer's wp-N.bin etc.).
 * Such a file must not get another GUI header, or the decoder sees
 * "GUI header -> GUI header -> JPEG".  It still gets EBXF because firmware
 * skips exactly 40 bytes before the GUI header.
 *
 * All four must hold:
 *   - type is a valid GUI_FormatType
 *   - w/h are in range
 *   - header size == file length - 16
 *   - body starts with a JPEG/PNG/GIF magic
 *
 * Checking all four avoids mistaking binary data for a header.
 */
static bool is_gui_wrapped(const uint8_t *b, size_t n, uint8_t *out_gui_type)
{
    if (n < GUI_IMG_HDR_LEN + 4) { return false; }

    static const uint8_t valid_types[] = {
        0, 1, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15,
        0x21, 0x22, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
        0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b
    };
    bool type_ok = false;
    for (size_t i = 0; i < sizeof(valid_types); i++)
    {
        if (b[1] == valid_types[i]) { type_ok = true; break; }
    }
    if (!type_ok) { return false; }

    int16_t w = (int16_t)(b[2] | (b[3] << 8));
    int16_t h = (int16_t)(b[4] | (b[5] << 8));
    if (w <= 0 || w > 2048 || h <= 0 || h > 2048) { return false; }

    uint32_t size = (uint32_t)b[8] | ((uint32_t)b[9] << 8) |
                    ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
    if (size != n - GUI_IMG_HDR_LEN) { return false; }

    const uint8_t *body = b + GUI_IMG_HDR_LEN;
    if (!((body[0] == 0xFF && body[1] == 0xD8) ||
          memcmp(body, "\x89PNG", 4) == 0 ||
          memcmp(body, "GIF8", 4) == 0)) { return false; }

    *out_gui_type = b[1];
    return true;
}

/* Wrap raw into the blob firmware expects.  Returns a malloc'ed buffer with
 * the total length in *out_len.
 *
 *   a) bare image     -> EBXF(40) + new GUI header(16) + raw
 *   b) GUI-wrapped    -> EBXF(40) + raw (no extra header)
 *
 * Both put the GUI header at offset 40.  key goes into EBXF's name field.
 * w/h are used only in case (a); the decoder overwrites them. */
static uint8_t *wrap_resource(const char *key, const uint8_t *raw, size_t raw_len,
                              uint16_t w, uint16_t h, bool with_ebxf,
                              size_t *out_len)
{
    uint8_t gui_type = 0;
    bool prewrapped = is_gui_wrapped(raw, raw_len, &gui_type);
    uint8_t eb_type = EB_FILE_TYPE_BIN;

    if (!prewrapped)
    {
        uint8_t gt;
        sniff(raw, raw_len, &eb_type, &gt);
        gui_type = gt;
        if (gt == 0xFF)
        {
            /* Neither an image nor GUI-wrapped: store as-is (e.g. raw bin). */
            uint8_t *copy = malloc(raw_len);
            if (copy) { memcpy(copy, raw, raw_len); *out_len = raw_len; }
            return copy;
        }
    }
    else
    {
        /* Prewrapped resource: map GUI type to EBXF file_type. */
        switch (gui_type)
        {
        case GUI_FMT_JPEG: eb_type = EB_FILE_TYPE_JPEG; break;
        case GUI_FMT_PNG:  eb_type = EB_FILE_TYPE_PNG;  break;
        case GUI_FMT_GIF:  eb_type = EB_FILE_TYPE_GIF;  break;
        default:           eb_type = EB_FILE_TYPE_BIN;  break;
        }
    }

    size_t gui_len   = prewrapped ? raw_len : (GUI_IMG_HDR_LEN + raw_len);
    size_t ebxf_len  = with_ebxf ? EBXF_HDR_LEN : 0;
    size_t total     = ebxf_len + gui_len;
    uint8_t *b = calloc(1, total);
    if (b == NULL) { return NULL; }

    /* --- EBXF header (outermost blob only; slots start at their GUI header) --- */
    if (with_ebxf)
    {
    uint8_t *e = b;
    e[0] = 'E'; e[1] = 'B'; e[2] = 'X'; e[3] = 'F';
    e[4] = 0x01;                       /* version */
    e[5] = eb_type;
    size_t klen = strlen(key);
    if (klen > 23) { klen = 23; }
    e[6] = (uint8_t)klen;
    e[7] = 0;
    put_le32(e + 8, (uint32_t)gui_len);      /* includes GUI header */
    put_le32(e + 12, crc32_ieee(0, raw, raw_len));
    memcpy(e + 16, key, klen);
    }

    if (prewrapped)
    {
        memcpy(b + ebxf_len, raw, raw_len);
    }
    else
    {
        struct gui_img_hdr *g = (struct gui_img_hdr *)(b + ebxf_len);
        g->flags = 0;
        g->type  = gui_type;
        g->w     = (int16_t)w;
        g->h     = (int16_t)h;
        g->size  = (uint32_t)raw_len;
        memcpy(b + ebxf_len + GUI_IMG_HDR_LEN, raw, raw_len);
    }

    *out_len = total;
    return b;
}

static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) { return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)n);
    if (buf == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Arguments */
/* ------------------------------------------------------------------ */

#define ENTRY_MAX_SLOT 4

struct entry
{
    /* A group is one BF record / key; pre/mask/part are optional slots. */
    const char *key;
    const char *path[ENTRY_MAX_SLOT];   /* [0]=data [1]=preview [2]=mask [3]=part */
    int         n_slots;
    uint8_t     id;                     /* RES_ID_* */
};

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --out <img.bin> --kvdb-size <bytes> --bf-size <bytes>\n"
            "          [--crc-skip <bytes>] --file <key>=<path> [--file ...]\n"
            "\n"
            "  --crc-skip     BF dirent CRC start offset (default 80 = EBXF40+index40)\n"
            "  --no-wrap      store raw without EBXF/GUI headers (firmware cannot decode)\n"
            "  --img-size WxH GUI header size (default 466x466; decoder overwrites it)\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *out_path  = NULL;
    uint32_t kvdb_size    = 0;
    uint32_t bf_size      = 0;
    size_t   crc_skip     = 80;   /* EBXF 40 + index 40 */
    int      wrap         = 1;        /* add EBXF/GUI headers */
    int      trim         = 1;        /* drop unused 0xFF tail */
    uint16_t img_w        = 466;      /* panel size; decoder overwrites it */
    uint16_t img_h        = 466;
    uint32_t xip_base     = 0;      /* KVDB absolute address */
    struct entry entries[64];
    int n_entries = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
        {
            out_path = argv[++i];
        }
        else if (strcmp(argv[i], "--kvdb-size") == 0 && i + 1 < argc)
        {
            kvdb_size = (uint32_t)strtoul(argv[++i], NULL, 0);
        }
        else if (strcmp(argv[i], "--bf-size") == 0 && i + 1 < argc)
        {
            bf_size = (uint32_t)strtoul(argv[++i], NULL, 0);
        }
        else if (strcmp(argv[i], "--xip-base") == 0 && i + 1 < argc)
        {
            xip_base = (uint32_t)strtoul(argv[++i], NULL, 0);
        }
        else if (strcmp(argv[i], "--crc-skip") == 0 && i + 1 < argc)
        {
            crc_skip = (size_t)strtoul(argv[++i], NULL, 0);
        }
        else if (strcmp(argv[i], "--full") == 0)
        {
            trim = 0;
        }
        else if (strcmp(argv[i], "--no-wrap") == 0)
        {
            wrap = 0;
        }
        else if (strcmp(argv[i], "--img-size") == 0 && i + 1 < argc)
        {
            char *s = argv[++i];
            char *x = strchr(s, 'x');
            if (x == NULL) { fprintf(stderr, "--img-size WxH\n"); return 2; }
            *x = '\0';
            img_w = (uint16_t)strtoul(s, NULL, 0);
            img_h = (uint16_t)strtoul(x + 1, NULL, 0);
        }
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
            if (n_entries >= (int)(sizeof(entries) / sizeof(entries[0])))
            {
                fprintf(stderr, "too many --file entries\n");
                return 2;
            }
            char *spec = argv[++i];
            char *eq = strchr(spec, '=');
            if (eq == NULL || eq == spec || eq[1] == '\0')
            {
                fprintf(stderr,
                        "--file expects <key>=<path>[,pre=<p>][,mask=<p>][,part=<p>]\n"
                        "   got '%s'\n", spec);
                return 2;
            }
            *eq = '\0';

            struct entry *e = &entries[n_entries];
            memset(e, 0, sizeof(*e));
            e->key = spec;

            /* Comma-separated slots: bare path is data, others use pre/mask/part. */
            char *rest = eq + 1;
            for (;;)
            {
                char *comma = strchr(rest, ',');
                if (comma) { *comma = '\0'; }

                char *val = rest;
                char *colon = strchr(rest, ':');
                int slot = 0;

                /* "pre=xxx.bin" form: detect the tag with '='. */
                char *inner_eq = strchr(rest, '=');
                if (inner_eq)
                {
                    *inner_eq = '\0';
                    const char *tag = rest;
                    val = inner_eq + 1;
                    if      (strcmp(tag, "data") == 0) { slot = 0; }
                    else if (strcmp(tag, "pre")  == 0) { slot = 1; }
                    else if (strcmp(tag, "mask") == 0) { slot = 2; }
                    else if (strcmp(tag, "part") == 0) { slot = 3; }
                    else
                    {
                        fprintf(stderr, "unknown slot '%s' in '%s'\n", tag, spec);
                        return 2;
                    }
                }
                else
                {
                    (void)colon;
                }

                if (e->path[slot] != NULL)
                {
                    fprintf(stderr, "duplicate slot %d in '%s'\n", slot, spec);
                    return 2;
                }
                e->path[slot] = val;
                if (slot + 1 > e->n_slots) { e->n_slots = slot + 1; }

                if (comma == NULL) { break; }
                rest = comma + 1;
            }

            if (e->path[0] == NULL)
            {
                fprintf(stderr, "entry '%s' has no data slot\n", spec);
                return 2;
            }
            n_entries++;
        }
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    /* Derive each group's resource type:
     *   has mask/part    -> SRC_TEARBADGE (0x85)
     *   has preview      -> SRC_IMG       (0x81)
     *   data only, image -> SRC_IMG
     *   data only, other -> SRC_VIDEO     (0x82)
     */
    for (int i = 0; i < n_entries; i++)
    {
        struct entry *e = &entries[i];
        if (e->path[2] || e->path[3])
        {
            e->id = RES_ID_TEAR;
        }
        else if (e->path[1])
        {
            e->id = RES_ID_IMG;
        }
        else
        {
            /* Tell image from video by the data slot. */
            size_t n = 0;
            uint8_t *h = read_file(e->path[0], &n);
            uint8_t eb, gui;
            if (h == NULL)
            {
                fprintf(stderr, "cannot read %s\n", e->path[0]);
                return 5;
            }
            sniff(h, n, &eb, &gui);
            uint8_t gt = 0;
            bool pre = is_gui_wrapped(h, n, &gt);
            free(h);
            e->id = (gui == 0xFF && !pre) ? RES_ID_VIDEO : RES_ID_IMG;
        }
    }

    if (xip_base == 0)
    {
        fprintf(stderr, "--xip-base is required (absolute address of the KVDB, "
                        "e.g. 0x70936000)\n");
        return 2;
    }
    if (out_path == NULL || kvdb_size == 0 || bf_size == 0)
    {
        usage(argv[0]);
        return 2;
    }
    if (kvdb_size % 4096 != 0 || bf_size % 4096 != 0)
    {
        fprintf(stderr, "kvdb/bf sizes must be 4K aligned\n");
        return 2;
    }

    /* Host-device layout: KVDB at 0, BF right after it.  The whole buffer is
     * flashed at the KVDB absolute address. */
    g_len = (size_t)kvdb_size + bf_size;
    g_img = malloc(g_len);
    if (g_img == NULL) { fprintf(stderr, "OOM (%zu)\n", g_len); return 3; }
    memset(g_img, 0xFF, g_len);
    nor_flash_host.len = g_len;

    /* fal_cfg.h provides the partition table (generated by pack.py).  Call
     * fal_init() first: fal_partition_find() needs it, while the real init
     * would only run inside fdb_kvdb_init(), after this check. */
    if (fal_init() <= 0)
    {
        fprintf(stderr, "fal_init failed -- fal_cfg.h without FAL_PART_HAS_TABLE_CFG?\n");
        return 3;
    }
    const struct fal_partition *p_kv = fal_partition_find("fdb_kvdb1");
    const struct fal_partition *p_bf = fal_partition_find("bf_data");
    if (p_kv == NULL || p_bf == NULL)
    {
        fprintf(stderr, "FAL partitions not found -- fal_cfg.h mismatch\n");
        return 3;
    }
    if ((uint32_t)p_kv->len != kvdb_size || (uint32_t)p_bf->len != bf_size)
    {
        fprintf(stderr,
                "size mismatch: fal_cfg.h has kvdb=0x%zx bf=0x%zx, argv wants "
                "kvdb=0x%x bf=0x%x\n",
                p_kv->len, p_bf->len, kvdb_size, bf_size);
        return 3;
    }

    static struct fdb_kvdb kvdb;
    static struct fdb_bf   bf;
    fdb_err_t rc = fdb_kvdb_init(&kvdb, "env", "fdb_kvdb1", NULL, NULL);
    if (rc != FDB_NO_ERR)
    {
        fprintf(stderr, "fdb_kvdb_init failed rc=%d\n", (int)rc);
        return 4;
    }
    rc = fdb_bf_init(&bf, &kvdb, "bf_data", NULL);
    if (rc != FDB_NO_ERR)
    {
        fprintf(stderr, "fdb_bf_init failed rc=%d\n", (int)rc);
        return 4;
    }

    /*
     * One entry = one BF record:
     *
     *   [EBXF 40B][index 40B][data GUI][preview GUI][mask GUI][part GUI]
     *
     * The index stores XIP absolute addresses.  BF allocates sequential
     * block-aligned runs, so the record address is known from bf_cursor and
     * the index can be filled before fdb_bf_create().
     */

    /* --xip-base is the KVDB absolute address; BF follows it. */
    uint32_t bf_xip_base = xip_base + kvdb_size;
    uint32_t bf_cursor   = 0;

    /* Per-group slot offsets inside the record (0 = absent). */
    for (int i = 0; i < n_entries; i++)
    {
        struct entry *e = &entries[i];

        /* ---- Build the blob ---- */
        size_t total = EBXF_HDR_LEN + RES_IDX_LEN;
        size_t slot_off[ENTRY_MAX_SLOT] = {0};

        uint8_t *parts[ENTRY_MAX_SLOT] = {0};
        size_t   part_len[ENTRY_MAX_SLOT] = {0};

        for (int s = 0; s < e->n_slots; s++)
        {
            if (e->path[s] == NULL) { continue; }
            size_t raw_len = 0;
            uint8_t *raw = read_file(e->path[s], &raw_len);
            if (raw == NULL)
            {
                fprintf(stderr, "cannot read %s\n", e->path[s]);
                for (int k = 0; k < s; k++) { free(parts[k]); }
                return 5;
            }

            uint8_t *w = raw;
            size_t   wlen = raw_len;
            if (wrap)
            {
                w = wrap_resource(e->key, raw, raw_len, img_w, img_h,
                                  false, &wlen);   /* slot: GUI header only */
                free(raw);
                if (w == NULL)
                {
                    fprintf(stderr, "wrap failed for %s\n", e->path[s]);
                    for (int k = 0; k < s; k++) { free(parts[k]); }
                    return 5;
                }
            }
            parts[s] = w;
            part_len[s] = wlen;
        }

        /* Tear badge: preview is rewritten in place on the device, so it gets
         * its own 4K-aligned, fixed-size slot (80 KB, tail filled with 0xFF).
         * The index length is the capacity; the GUI header gives the real
         * image size. */
        if (e->id == RES_ID_TEAR)
        {
            uint8_t *slot = malloc(TEAR_PREVIEW_CAP);
            if (slot == NULL)
            {
                fprintf(stderr, "OOM (preview slot)\n");
                for (int k = 0; k < ENTRY_MAX_SLOT; k++) { free(parts[k]); }
                return 5;
            }
            memset(slot, 0xFF, TEAR_PREVIEW_CAP);
            if (parts[1] != NULL)
            {
                if (part_len[1] > TEAR_PREVIEW_CAP)
                {
                    fprintf(stderr, "preview %zu B exceeds reserved %u B\n",
                            part_len[1], (unsigned)TEAR_PREVIEW_CAP);
                    free(slot);
                    for (int k = 0; k < ENTRY_MAX_SLOT; k++) { free(parts[k]); }
                    return 5;
                }
                memcpy(slot, parts[1], part_len[1]);
                free(parts[1]);
            }
            parts[1] = slot;
            part_len[1] = TEAR_PREVIEW_CAP;
            if (e->n_slots < 2) { e->n_slots = 2; }
        }

        /* Fixed slot order: data, preview, mask, part.  Slots are 8-byte
         * aligned (the record itself is 4K aligned); tear preview is 4K
         * aligned so the device can rewrite it in place. */
        for (int s = 0; s < e->n_slots; s++)
        {
            if (parts[s] == NULL) { continue; }
            size_t align = (e->id == RES_ID_TEAR && s == 1) ? 4096u : RES_ALIGN;
            total = (total + align - 1) & ~(align - 1);
            slot_off[s] = total;
            total += part_len[s];
        }

        uint8_t *blob = calloc(1, total);
        if (blob == NULL)
        {
            fprintf(stderr, "OOM (%zu)\n", total);
            for (int s = 0; s < ENTRY_MAX_SLOT; s++) { free(parts[s]); }
            return 5;
        }

        uint8_t *idxp = blob + EBXF_HDR_LEN;

        /* --- EBXF header --- */
        blob[0]='E'; blob[1]='B'; blob[2]='X'; blob[3]='F';
        blob[4] = 0x01;
        blob[5] = (e->id == RES_ID_VIDEO) ? EB_FILE_TYPE_BIN : EB_FILE_TYPE_JPEG;
        size_t klen = strlen(e->key);
        if (klen > 23) { klen = 23; }
        blob[6] = (uint8_t)klen;
        memcpy(blob + 16, e->key, klen);
        put_le32(blob + 8, (uint32_t)(total - EBXF_HDR_LEN));
        put_le32(blob + 12, crc32_ieee(0, idxp, total - EBXF_HDR_LEN));

        /* --- Index header ---
         * BF allocates from write_cursor in block-aligned runs, so this
         * record's address is known before fdb_bf_create(). */
        size_t cap = (total + 4095u) / 4096u * 4096u;
        uint32_t rec_xip = bf_xip_base + bf_cursor;
        bf_cursor += cap;

        idxp[0] = e->id;
        put_le32(idxp + RES_IDX_DATA,
                 slot_off[0] ? rec_xip + slot_off[0] : 0);
        put_le32(idxp + RES_IDX_PREVIEW,
                 slot_off[1] ? rec_xip + slot_off[1] : 0);
        put_le32(idxp + RES_IDX_MASK,
                 slot_off[2] ? rec_xip + slot_off[2] : 0);
        put_le32(idxp + RES_IDX_PART,
                 slot_off[3] ? rec_xip + slot_off[3] : 0);

        /* Real slot lengths, excluding alignment padding. */
        put_le32(idxp + RES_IDX_DATA_LEN,
                 slot_off[0] ? (uint32_t)part_len[0] : 0);
        put_le32(idxp + RES_IDX_PREVIEW_LEN,
                 slot_off[1] ? (uint32_t)part_len[1] : 0);
        put_le32(idxp + RES_IDX_MASK_LEN,
                 slot_off[2] ? (uint32_t)part_len[2] : 0);
        put_le32(idxp + RES_IDX_PART_LEN,
                 slot_off[3] ? (uint32_t)part_len[3] : 0);

        /* --- Slot data --- */
        for (int s = 0; s < ENTRY_MAX_SLOT; s++)
        {
            if (parts[s] == NULL) { continue; }
            memcpy(blob + slot_off[s], parts[s], part_len[s]);
            free(parts[s]);
            parts[s] = NULL;
        }

        if (fdb_bf_exists(&bf, e->key))
        {
            fprintf(stderr, "duplicate key '%s'\n", e->key);
            free(blob);
            return 5;
        }

        fdb_bf_file_t fh = NULL;
        rc = fdb_bf_create(&bf, e->key, total, &fh);
        if (rc != FDB_NO_ERR)
        {
            fprintf(stderr, "fdb_bf_create('%s', %zu) rc=%d\n", e->key, total, (int)rc);
            free(blob);
            return 6;
        }
        rc = fdb_bf_append(fh, blob, total);
        if (rc != FDB_NO_ERR)
        {
            fprintf(stderr, "fdb_bf_append('%s') rc=%d\n", e->key, (int)rc);
            (void)fdb_bf_abort(fh);
            free(blob);
            return 6;
        }
        uint32_t crc = crc_from(blob, total, crc_skip);
        rc = fdb_bf_commit(fh, &crc);
        if (rc != FDB_NO_ERR)
        {
            fprintf(stderr, "fdb_bf_commit('%s') rc=%d\n", e->key, (int)rc);
            free(blob);
            return 6;
        }
        free(blob);

        printf("  + %-8s id=0x%02X %8zu B", e->key, e->id, total);
        for (int s = 0; s < e->n_slots; s++)
        {
            if (e->path[s]) { printf("  [%d]%s", s, strrchr(e->path[s], '/') ? strrchr(e->path[s], '/') + 1 : e->path[s]); }
        }
        printf("\n");
    }

    /* Re-read the directory: every key must resolve (cheap write/read check). */
    for (int i = 0; i < n_entries; i++)
    {
        uint32_t addr = 0;
        size_t   size = 0;
        if (fdb_bf_get_addr(&bf, entries[i].key, &addr, &size) != FDB_NO_ERR)
        {
            fprintf(stderr, "verify failed: '%s' not readable\n", entries[i].key);
            return 7;
        }
    }

    /*
     * Trim the unused 0xFF tail.
     *
     * Ending the BF data at the last dirent's end (rounded up to a block) is
     * safe:
     *   - fdb_bf_init() only scans dirents in the KVDB, not the data area
     *   - reads stay inside [offset, offset+capacity)
     *   - unflashed flash reads as 0xFF, identical to writing the full area
     *
     * The KVDB must stay whole: init walks every sector and writes headers.
     *
     * --full disables trimming and keeps the image BF-partition sized.
     */
    size_t write_len = g_len;
    if (trim)
    {
        size_t bf_end = 0;
        for (int i = 0; i < n_entries; i++)
        {
            uint32_t addr = 0;
            size_t   sz = 0;
            if (fdb_bf_get_addr(&bf, entries[i].key, &addr, &sz) == FDB_NO_ERR)
            {
                /* addr is XIP absolute; convert to a BF-partition offset. */
                size_t rel = (size_t)(addr - nor_flash_host.addr - kvdb_size);
                size_t cap = (sz + 4095u) / 4096u * 4096u;
                if (rel + cap > bf_end) { bf_end = rel + cap; }
            }
        }
        if (bf_end == 0) { bf_end = 4096; }        /* empty DB: keep one sector */
        write_len = kvdb_size + bf_end;
        printf("  trim: BF used %zu B, image %zu -> %zu B (saves %.1f MB)\n",
               bf_end, g_len, write_len, (double)(g_len - write_len) / 1048576.0);
    }

    FILE *fo = fopen(out_path, "wb");
    if (fo == NULL)
    {
        fprintf(stderr, "cannot write %s: %s\n", out_path, strerror(errno));
        return 8;
    }
    if (fwrite(g_img, 1, write_len, fo) != write_len)
    {
        fprintf(stderr, "short write to %s\n", out_path);
        fclose(fo);
        return 8;
    }
    fclose(fo);

    printf("wrote %s (%zu B, %d file(s))\n", out_path, write_len, n_entries);
    free(g_img);
    return 0;
}
