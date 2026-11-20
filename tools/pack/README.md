# tools/pack — app/romfs merge packing + prebuilt resource FDB packing

Two goals:

1. **Ship app and UI assets as one image**, so a single OTA delivers both and the versions always match.
2. **Flash the prebuilt wallpapers straight into the FDB region in FlashDB format**, so `flashdb_prepare()` can enumerate them at boot.

All addresses come from [`layout.json`](layout.json). After changing the flash plan, run `check` first.

---

## 1. `check` — layout consistency

```bash
python3 tools/pack/pack.py check
```

It compares `layout.json` against **every hardcoded** address in the project, one by one:

| Compared item | Location |
|---|---|
| `BANK{0,1}_APP_ADDR/SIZE` | `flash_map.h` |
| app partition | this project's `snippets/flash_32M_bank{0,1}/*.overlay` (currently in use)<br>`realtek-app/snippets/flash_{4,8,16}M_bank{0,1}/*.overlay` (spare variants) |
| FDB region absolute address/size | `port/flashdb/fal_cfg.h` |
| romfs mount | `app/designer/src/Mi_eBadgeEntry.c`, `app/designer/project.json` |

It only reports mismatches and does not decide which side is right — the plan decides. The current `layout.json` matches the project.

---

## 2. `app` — merge app + romfs

```bash
python3 tools/pack/pack.py app \
    --bank bank0 \
    --app bin/bank0/app_bank0.bin \
    --romfs app/designer/build/app_romfs_0x70536400.bin \
    --out bin/bank0/app_bank0_packed.bin \
    --sign
```

What it does, in order:

1. Verify the input is a bin with an image header (`IMGHDR`) and that `payload_len + 0x400 == file length`.
2. Compute the romfs target = `app base + offset`, and check for overlap and 64-byte alignment.
3. **Relocate romfs**: mkromfs writes `name` / `data` as **absolute flash addresses**,
   so changing the target requires shifting them all, otherwise directory entries dereference wild addresses. The tool walks
   every reachable directory entry recursively and does `+= delta`, then checks that every pointer falls inside this romfs.
4. Concatenate `app ++ padding ++ romfs`.
5. Size check: refuse outright when it exceeds `BANK*_APP_SIZE` (OTA's
   `app_ota_check_section_size` would refuse the same way; failing early beats finding out after flashing).
6. `prepend_header /app_code` updates `payload_len`; with `--sign` the ECDSA signature is redone
   (the payload changed, so it must be re-signed or the boot check fails).
7. Optionally `--mp` adds the MP header (used by mpcli to identify the download address).
8. **Write `romfs_layout.h`** (`app/designer/src/` or `src/user/` in newer Designer versions; both are written when both exist) — the `ROMFS_OFFSET` the firmware mount needs.
9. Self-check: re-parse the artifact header and the relocated romfs pointers.

### romfs target: an offset, not an absolute address

By default romfs follows the end of app (64-byte aligned). The firmware mounts it with a **run-time** expression:

```c
extern unsigned char __rom_region_start[];   /* linker: load base of this image */
gui_vfs_mount_romfs("/", (void *)(__rom_region_start + ROMFS_OFFSET), 0);
```

The reason: `__rom_region_start` is `0x7009E000` for bank0 and `0x704F3000` for bank1,
while the distance between romfs and app is fixed. An absolute constant would point at the wrong place in **the other bank** — the symptom is
garbled directory entries / failed file opens, and it only appears after switching banks, making it hard to track down.

> Note: the third parameter of `gui_vfs_mount_romfs()`, `base_offset`, is not used by the implementation,
> so base correction cannot rely on it; the pointers must already be correct inside the image.

### Why the old fixed offset `0x498400` cannot be reused

The old romfs for bank0 sits at `0x70536400`, `0x498400` away from the app base. But the end of app only reaches
`0x7018FE84` — the 3.6MB in between is blank. When merging, that blank turns into real padding bytes,
larger than the whole app partition (1.41MB), so it can never pass the size check.

---

## 3. `fdb` — prebuilt wallpapers (FlashDB format)

Assets live in `fdb_resource_bin/` by default (tracked with the project), and the product is written to the same directory:

```bash
python3 tools/pack/pack.py fdb \
    --out tools/pack/fdb_resource_bin/fdb_prebuilt.bin \
    --file wp1=tools/pack/fdb_resource_bin/wp-1.bin \
    --file wp2=tools/pack/fdb_resource_bin/wp-2.bin
```

The product covers both the `kvdb + bf_data` regions; flash it to `kvdb_addr` from `layout.json`:

```bash
mpcli -c /dev/ttyUSB0 -p -A 0x70936000 -F tools/pack/fdb_resource_bin/fdb_prebuilt.bin -M 5 -r -u -d -T RTL87X3G
```

### Why build on the host instead of stitching bytes in a script

The KVDB sector header is a **bit-by-bit state table** (`0xFF -> 0x7F -> 0x3F ...`), KV headers carry CRCs,
and there is GC plus record versions; BF directory entries are KV values whose `offset` is relative to the start of bf_data and whose `capacity`
is block-aligned. Getting any one of those wrong for the library version produces **silent dirty data** — at boot the
"database looks corrupt" but no step can be identified as the culprit.

So the `fdb` subcommand compiles the very FlashDB the firmware uses (`component/FlashDB`),
replacing only the lowest-level FAL flash operations with file reads and writes. The produced bytes are exactly
what the device itself writes through `fdb_bf_create/append/commit`.

After generating, it automatically **reads the image back to verify** (`fdb_host_verify`): again `kvdb_init -> bf_init -> bf_foreach`,
and what it prints is what `bf_boot_enum_cb()` will see at boot; a missing key or an auto-formatted image counts as failure.

### Resource blob format

One BF record = one resource group:

| Section | Length | Description |
|---|---|---|
| EBXF header | 40 B | magic/version/type/name/size/crc32; present only at the outermost level of the group |
| resource index header | 40 B | see the table below |
| padding | 0-7 B | zero padding when a slot length is not a multiple of 8, keeping every slot address 8-byte aligned |
| data GUI | — | `gui_rgb_data_head_t`(8) + size(4) + pad(4) + the image data |
| preview GUI | — | same as above, optional; **SRC_TEARBADGE is fixed at 80 KB, 4K aligned and can be rewritten in place by the device** |
| mask GUI | — | SRC_TEARBADGE only |
| part GUI | — | SRC_TEARBADGE only |

> For `SRC_TEARBADGE`, the preview is padded with 0 up to a 4K boundary and `TEAR_PREVIEW_CAP` is reserved
> (80 KB, tail 0xFF). The preview length in the index is the **capacity**; the actual image size comes from the `size`
> in the slot's GUI header. When saving, the device only erases and rewrites those 80 KB, leaving the record itself and the `.raw` unchanged,
> so deleting the `.raw` frees the whole badge. After a device rewrite the crc32 in EBXF goes stale,
> and the firmware does not check it.

Index header (all little-endian, addresses are **absolute XIP addresses**):

| Offset | Length | Field |
|---|---|---|
| 0 | 4 | `id`: `0x81` SRC_IMG / `0x82` SRC_VIDEO / `0x85` SRC_TEARBADGE |
| 4 | 4 | GUI header address of data |
| 8 | 4 | GUI header address of preview (0 = none) |
| 12 | 4 | tear_mask (tear only, 0 = none) |
| 16 | 4 | tear_part (tear only, 0 = none) |
| 20 | 4 | data length (0 = slot absent) |
| 24 | 4 | preview length |
| 28 | 4 | tear_mask length |
| 32 | 4 | tear_part length |
| 36 | 4 | reserved (0) |

The length is the real byte count of the slot content and does **not include inter-slot alignment padding**. Old records have no length field
(20..23 were reserved=0 back then); they are read as 0 and fall back to "derived from the neighbouring address / end of record".

The index header is 40 B, so `EBXF(40) + index header(40) = 80`, and the GUI header of the first slot is naturally
8-byte aligned. When a later slot length is not a multiple of 8, the packer pads it with zeros.

The firmware reads this table directly and **no longer pairs by key** (in `mainface_list_init()`
`idx = raw + 40`, then the four addresses are assigned to `data` / `img_preview` /
`param0` / `param1`).

> **VIDEO**: data points at the video payload, without a GUI header; preview/mask/part are ignored.
> The EBXF size/crc may be left empty for VIDEO.

### Group syntax

```bash
# one group: data is required, pre/mask/part are optional
--file "wp1=wp-1.bin,pre=pre-1.bin"
--file "tb1=wp-tear.bin,pre=pre-1.bin,mask=tear_mask.bin,part=tear_part.bin"
```

`--assets-dir` pairs `wp-N.bin` with `pre-N.bin` by index into groups automatically.

> **Note on key naming**: a key starting with `tear` is routed by the firmware as a user-uploaded preview
> (`TEAR_BF_KEY_PREFIX`), so tear groups use a prefix such as `tb1`.

### CRC coverage

| CRC | Covers |
|---|---|
| crc32 in the EBXF header | index header + the GUI of every slot (everything after EBXF) |
| FDB directory entry data_crc | the same, with `--crc-skip 80` (EBXF 40 + index header 40) |

**The tool wraps automatically** (on by default). Any input format works — PNG / JPEG / GIF are sniffed by
file header and `file_type` and the GUI `type` are filled in; unrecognized files are stored as-is as BIN.

Why wrapping is required: the firmware reads wallpapers in `ViewMainFace_user.c:296`

```c
data = (uint8_t *)raw + 40;                    // hardcoded skip of 40 bytes
gui_img_create_from_mem(..., data, ...);       // data must point at a GUI image header
```

If the image were stored bare, `raw+40` would land in the middle of the image, `type/w/h` would all be garbage and nothing would display.

Related options:

| Option | Default | Description |
|---|---|---|
| `--no-wrap` | off | no wrapping, stored as-is (the firmware cannot read it, debugging only) |
| `--img-size WxH` | `466x466` | width/height in the GUI header. The decoder overrides them with the real image size |
| `--crc-skip N` | `80` | start offset of the directory entry CRC (= EBXF 40 + index header 40) |

Boot enumeration (`bf_boot_enum_cb()`) only reads directory entries and does not check `data_crc`, so this value
only affects the read-back verification. The upload path (`xfer_session.c`) writes a different kind of record, using
`ebadge_port_storage_wp_commit(..., EBXF_HDR_LEN, ...)` (skipping 40 bytes).

> The tool **does not transcode**: PNG stays PNG, JPEG stays JPEG. The firmware links stb_image's
> PNG/JPEG decoders, so both display. To reduce the encoded size, convert to a
> low-quality JPEG with Pillow or similar before feeding it in.

---

## 4. Hooking into the build scripts

```bash
# WSL / Linux
./tools/scripts/build.sh --bank 0 --pack                      # use the default romfs
./tools/scripts/build.sh --bank 0 --pack --romfs /path/x.bin
./tools/scripts/build.sh --bank both --pack                   # pack both banks
```

```powershell
# Windows
.\tools\scripts\build.ps1 -Bank 0 -Pack
.\tools\scripts\build.ps1 -Bank 0 -Pack -Romfs D:\path\x.bin
.\tools\scripts\build.ps1 -Bank both -Pack
```

`--pack` iterates to convergence: pack -> incremental rebuild if `ROMFS_OFFSET` changed -> pack again.
Usually 1-2 rounds (an incremental rebuild takes about 15 s), at most 6.

When the size check fails it does **not** rewrite `romfs_layout.h`, so a failed pack does not pollute the firmware configuration.

---

## 5. Flashing romfs only (the fast path after changing wallpapers)

`--pack` places romfs at a **4KB-aligned** address, so it occupies whole erase sectors and shares none with app.
That makes it possible to flash only the 1.58MB romfs after changing assets, instead of reflashing the 2.52MB merged image.

```bash
# WSL / Linux
./tools/scripts/build.sh --bank 0 --pack      # generate bin/bank0/app_romfs_bank0_reloc.bin
./tools/scripts/flash_romfs.sh                # flash romfs only (default bank0 / /dev/ttyUSB0)
./tools/scripts/flash_romfs.sh --bank 1 --port /dev/ttyUSB1
./tools/scripts/flash_romfs.sh --dry-run      # print the command only
```

```powershell
# Windows
.\tools\scripts\build.ps1 -Bank 0 -Pack
.\tools\scripts\flash_romfs.ps1 -Bank 0
.\tools\scripts\flash_romfs.ps1 -Bank 1 -Port COM7
.\tools\scripts\flash_romfs.ps1 -DryRun
```

**You must use `_reloc.bin`**, not `app/designer/build/app_romfs_*.bin`:
the latter's directory entries still hold the old absolute address from mkromfs (`0x70536400`), and flashing it turns the UI black.
`_reloc.bin` is the already-relocated copy, byte-identical to the corresponding section of the merged image.

Before flashing, `tools/scripts/flash_romfs.sh` rejects two cases:

- `ROMFS_OFFSET` is not a multiple of 4KB -> erasing would reach into app's sector
- the first directory pointer in the reloc file != app base + current offset -> the file was built for another offset

## 6. Known limitations

- **user data is not A/B**: `dfu_get_user_data_info()` returns `USER_DATA1_ADDR` directly and
  OTA writes in place. So "app and resources match" holds, but **a power loss cannot be rolled back** — if power fails halfway through writing romfs,
  the new app has no UI resources. Atomicity would require A/B for user data, costing twice the space on 32M.
- **Prebuilt wallpapers share the BF namespace with user wallpapers** (key `wp<N>`). "Clear data"
  (`fdb_bf_reset(FDB_BF_RESET_DIR_ONLY)`) deletes the prebuilt wallpapers along with everything else.
  If that is unacceptable, factory-fixed resources belong in romfs and BF should hold only user content.
- Items with `_todo` in `layout.json` are known to need fixing (currently the FDB region coordinates).
