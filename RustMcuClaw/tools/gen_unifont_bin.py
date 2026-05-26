#!/usr/bin/env python3
"""gen_unifont_bin.py — Convert GNU Unifont .hex to a flat PSRAM-loadable binary.

Binary format written to <output.bin>:
  [0..3]   Magic: 0x55 0x4E 0x49 0x46  ("UNIF")
  [4..7]   Version: 0x00000001 (big-endian)
  [8..11]  BMP entry count: 65536 (big-endian)
  [12..]   65536 entries × 33 bytes each
             byte  0   : glyph width in pixels (0 = absent, 8 = halfwidth, 16 = fullwidth)
             bytes 1-32: 16 rows × uint16_t (big-endian)
                         Halfwidth: row pixel bits in bits [15:8], bits [7:0] = 0
                         Fullwidth: row pixel bits in bits [15:0]
                         In both cases bit 15 = leftmost column, MSB-first.

Total file size: 12 + 65536 × 33 = 2 162 700 bytes (~2.06 MB).

Download unifont hex from:
  https://unifoundry.com/pub/unifont/unifont-15.1.05/font-builds/unifont-15.1.05.hex.gz
  (or any other version; the format is the same)

Usage:
  python gen_unifont_bin.py unifont-15.1.05.hex.gz unifont.bin
  python gen_unifont_bin.py unifont-15.1.05.hex    unifont.bin  # uncompressed also works

Then copy unifont.bin to the SD card at:
  /SD:/RustMcuClaw/unifont.bin
"""

import gzip
import sys
import struct

MAGIC        = b'UNIF'
VERSION      = 1
BMP_SIZE     = 65536
ENTRY_SIZE   = 33       # 1 byte width + 32 bytes (16 × uint16_t big-endian)
HEADER_SIZE  = 12       # magic(4) + version(4) + bmp_size(4)
TOTAL_BYTES  = HEADER_SIZE + BMP_SIZE * ENTRY_SIZE  # 2 162 700


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    hex_path = sys.argv[1]
    out_path = sys.argv[2]

    # Pre-allocate: all entries start as "absent" (width=0, data=zeros).
    data = bytearray(BMP_SIZE * ENTRY_SIZE)

    loaded = 0
    skipped = 0

    print(f"Reading {hex_path} …")
    open_fn = gzip.open if hex_path.endswith(".gz") else open
    with open_fn(hex_path, "rt", encoding="utf-8", errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            raw = raw.strip()
            if not raw or ":" not in raw:
                continue

            cp_str, hex_data = raw.split(":", 1)
            hex_data = hex_data.strip()

            try:
                cp = int(cp_str, 16)
            except ValueError:
                print(f"  line {lineno}: bad codepoint '{cp_str}', skipping")
                skipped += 1
                continue

            if cp >= BMP_SIZE:
                # Supplementary planes — not supported by this flat BMP index.
                skipped += 1
                continue

            n = len(hex_data)
            if n == 32:
                # Halfwidth: 16 bytes (one byte per row).
                width = 8
                try:
                    raw_bytes = bytes.fromhex(hex_data)
                except ValueError:
                    skipped += 1
                    continue
                # Store each row byte in the HIGH byte of the uint16.
                row_words = [b << 8 for b in raw_bytes]
            elif n == 64:
                # Fullwidth: 32 bytes (two bytes per row, big-endian).
                width = 16
                try:
                    raw_bytes = bytes.fromhex(hex_data)
                except ValueError:
                    skipped += 1
                    continue
                row_words = [
                    (raw_bytes[i * 2] << 8) | raw_bytes[i * 2 + 1]
                    for i in range(16)
                ]
            else:
                print(f"  line {lineno}: codepoint U+{cp:04X} unexpected hex length {n}, skipping")
                skipped += 1
                continue

            offset = cp * ENTRY_SIZE
            data[offset] = width
            for i, word in enumerate(row_words):
                data[offset + 1 + i * 2]     = (word >> 8) & 0xFF
                data[offset + 1 + i * 2 + 1] =  word       & 0xFF

            loaded += 1

    print(f"Glyphs loaded: {loaded}, skipped/SMP: {skipped}")

    print(f"Writing {out_path} …")
    with open(out_path, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack(">I", VERSION))
        fh.write(struct.pack(">I", BMP_SIZE))
        fh.write(data)

    size = HEADER_SIZE + len(data)
    print(f"Done. Output: {size:,} bytes ({size / 1024 / 1024:.2f} MiB)")
    print()
    print("Copy unifont.bin to the SD card at:")
    print("  /SD:/RustMcuClaw/unifont.bin")


if __name__ == "__main__":
    main()
