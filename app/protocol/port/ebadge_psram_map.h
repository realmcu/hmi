/**
 * @file    ebadge_psram_map.h
 * @brief   Single authority for how the first 4 MB of SPIC1 *and* of SPIC3 PSRAM
 *          is divided up.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 * ---------------------------------------------------------------------------
 * Several unrelated modules carve out of the same PSRAM windows and none of them
 * used to know about the others:
 *
 *   - port/ui/gui_port_os.c        HoneyGUI's "lower" TLSF heap, by base+size
 *   - snippets/wifi/wifi.overlay   psram1_nc, by absolute DT address
 *   - app/protocol/wifi_xfer       the receive cache added for store-after-verify
 *   - app/protocol/wifi_ota        the 8711 OTA image staging buffer
 *
 * They had already collided once: the GUI heap was declared as SPIC1_MEM_BASE +
 * 0x400000, i.e. the whole MPU region, while psram1_nc sits at 0x22380000 --
 * 128 KB *inside* that heap.  Nothing detected it, because a TLSF heap only
 * reaches its top under memory pressure, and the SPI slot buffers up there are
 * written by GDMA rather than by anything the heap would trip over.  It would
 * have surfaced as PSRAM corruption under load, blamed on DMA.
 *
 * So the division lives here, once, with BUILD_ASSERTs that fail the build
 * instead of the device.  Both C consumers include this; the overlay cannot, so
 * the asserts at the bottom check the DT node against these numbers.
 *
 * ---------------------------------------------------------------------------
 * WHY EVERYTHING STAYS UNDER 0x22400000 / 0x24400000
 * ---------------------------------------------------------------------------
 * app_lower_init.c:app_mpu_config() maps exactly SPIC1_MEM_BASE..+0x400000 as
 * MPU region 2 and SPIC3_MEM_BASE..+0x400000 as MPU region 4, both with
 * attribute 0x44, i.e. non-cacheable.  Two consequences that both matter:
 *
 *   - Anything above those limits has whatever attributes the default map gives
 *     it, and this app never validated that PSRAM there is even mapped.  Not a
 *     place to put a buffer.
 *   - Everything below them is non-cacheable, so it needs no cache maintenance
 *     -- which is what makes the region usable for GDMA (psram1_nc) and is why
 *     the receive cache does not need the SCB_CleanDCache_by_Addr dance that
 *     H264 render buffers need on a cacheable PSRAM window.
 *
 * The cost is that reads are the slow non-cached path on BOTH devices -- SPIC3
 * is not the fast alternative it once was, since region 4 got the same 0x44.
 * For the two big staging buffers that is a fair trade: each is touched twice
 * per transfer (memcpy in, read back once for the flash append or the SPI push)
 * and never in a display-rate loop.  If a buffer here ever does need
 * cache-speed reads it takes an MPU region of its own; moving it between SPIC1
 * and SPIC3 buys nothing.
 *
 * ---------------------------------------------------------------------------
 * THE MAP -- SPIC1 (MPU region 2)
 * ---------------------------------------------------------------------------
 *   0x22000000  +3   MB   HoneyGUI lower heap
 *   0x22300000  +512 KB   unclaimed (the Wi-Fi receive cache used to live here)
 *   0x22380000  +128 KB   psram1_nc: SPI slot buffers, GDMA target
 *   0x223A0000  +128 KB   preview frame reassembly buffer
 *   0x223C0000  +256 KB   unclaimed
 *   0x22400000            end of the non-cacheable MPU region
 *
 * The 512 KB hole below psram1_nc is deliberately left unclaimed rather than
 * folded back into the GUI heap.  Growing that heap is a change in GUI memory
 * behaviour -- it only shows up under pressure, at which point the surfaces that
 * newly fit are the ones that change what gets drawn -- and nothing asked for
 * it.  It is contiguous and documented, so whichever module needs it next can
 * take it in one line.
 *
 * The preview buffer sits ABOVE psram1_nc, which breaks the "contiguous from the
 * bottom" arrangement.  Deliberate: psram1_nc's address is fixed in a .overlay,
 * and moving it to preserve the ordering would be a DT change for cosmetics.
 *
 * ---------------------------------------------------------------------------
 * THE MAP -- SPIC3 (MPU region 4)
 * ---------------------------------------------------------------------------
 *   0x24000000  +1   MB   Wi-Fi receive cache (whole file, before flash)
 *   0x24100000  +2   MB   8711 OTA image staging (whole image, before AT+OTAUSER)
 *   0x24300000  +1   MB   unclaimed
 *   0x24400000            end of the non-cacheable MPU region
 *
 * SPIC3 was completely unallocated by this app until these two moved in: the
 * GUI heap is on SPIC1 here (unlike the sibling apps eBadge_8773g and
 * simple-test, where it is on SPIC3), and a grep for SPIC3_MEM_BASE used to find
 * only a speed-report helper nobody calls.  That is what makes it the right home
 * for buffers this large -- 3 MB of staging does not fit alongside a 3 MB GUI
 * heap in one 4 MB window, and it does not have to.
 *
 * Both buffers are pure staging with a single owner and mutually exclusive
 * sessions, so neither needs the other's space and there is no reason to overlay
 * them.  Keeping them separate means a stuck OTA cannot corrupt a file transfer
 * that started after it, which an overlay would make possible.
 */
#ifndef _EBADGE_PSRAM_MAP_H_
#define _EBADGE_PSRAM_MAP_H_

#include <stdint.h>
#include <zephyr/toolchain.h>
#include <zephyr/devicetree.h>

#include "address_map.h"        /* SPIC1_MEM_BASE, SPIC3_MEM_BASE */

#ifdef __cplusplus
extern "C" {
#endif

/** Total span the MPU maps non-cacheable on each device; the hard ceiling for
 *  everything here.  Mirrors app_lower_init.c:app_mpu_config(). */
#define EB_PSRAM1_NC_SPAN            0x400000u
#define EB_PSRAM3_NC_SPAN            0x400000u

/*----------------------------------------------------------------------------*
 *  SPIC1: HoneyGUI lower heap
 *
 *  Was the full 0x400000.  Reduced to 3 MB when the receive cache was carved
 *  out below psram1_nc, and left at 3 MB after that cache moved to SPIC3 -- see
 *  the note about the resulting hole in the file header.
 *
 *  What this heap actually holds is HoneyGUI's off-screen surfaces and decoded
 *  image buffers, sized by widget usage rather than by anything fixed, so there
 *  is no formula that says 3 MB is enough -- only that the app has to fit.  A
 *  heap exhaustion here shows up as gui_lower_malloc returning NULL, which the
 *  GUI logs, so it is loud rather than silent.
 *----------------------------------------------------------------------------*/
#define EB_PSRAM_GUI_LOWER_BASE      (SPIC1_MEM_BASE)
#define EB_PSRAM_GUI_LOWER_SIZE      0x300000u        /* 3 MB */

/** First byte after the GUI heap.  psram1_nc must not start below this. */
#define EB_PSRAM_GUI_LOWER_END       (EB_PSRAM_GUI_LOWER_BASE + \
                                      EB_PSRAM_GUI_LOWER_SIZE)

/*----------------------------------------------------------------------------*
 *  SPIC1: Preview frame reassembly buffer
 *
 *  A whole preview JPEG is assembled here before it is handed to the decoder,
 *  which is new: the frame used to arrive pre-framed in JPGS slots and could be
 *  passed through chunk by chunk with the geometry taken from the slot header.
 *  On a raw TCP stream there is no geometry until FF D9 has been seen, so there
 *  is nothing to pass a decoder until the frame is whole.
 *
 *  128 KB against a 480x480-ish preview JPEG (~20..40 KB at any sane quality) is
 *  headroom, with one bonus the staging buffers do not get: a frame that
 *  overflows is dropped and the stream resynchronises on the next FF D8, so the
 *  cost of being wrong here is a dropped frame rather than a failed transfer.
 *
 *  Stayed on SPIC1 when the receive cache left.  It is 128 KB pinned by an
 *  absolute address that stream_session.c and the size ladder in the STP
 *  producer both agree on, and moving it would be churn for no gain.
 *----------------------------------------------------------------------------*/
#define EB_PSRAM_STREAM_FRAME_BASE   0x223A0000u
#define EB_PSRAM_STREAM_FRAME_SIZE   0x20000u         /* 128 KB */

#define EB_PSRAM_STREAM_FRAME_END    (EB_PSRAM_STREAM_FRAME_BASE + \
                                      EB_PSRAM_STREAM_FRAME_SIZE)

/*----------------------------------------------------------------------------*
 *  SPIC3: Wi-Fi receive cache
 *
 *  A whole received file is assembled here before any of it reaches flash, so
 *  its size is the real cap on what can be received -- see XS_MAX_FILE_SIZE in
 *  xfer_session.c, which is derived from this rather than declared next to it.
 *
 *  1 MB against a ~46 KB wallpaper is deliberate headroom, not sizing to the
 *  known case: the cap is announced to the App up front (0x16 TOO_LARGE at offer
 *  time), so the only thing a bigger buffer buys is not having to revisit this
 *  when the panel or the image format changes.
 *----------------------------------------------------------------------------*/
#define EB_PSRAM_XFER_CACHE_BASE     (SPIC3_MEM_BASE)
#define EB_PSRAM_XFER_CACHE_SIZE     0x100000u        /* 1 MB */

#define EB_PSRAM_XFER_CACHE_END      (EB_PSRAM_XFER_CACHE_BASE + \
                                      EB_PSRAM_XFER_CACHE_SIZE)

/*----------------------------------------------------------------------------*
 *  SPIC3: 8711 OTA image staging
 *
 *  The whole OTA image is buffered here before one byte of it is pushed to the
 *  8711, for the same reason the receive cache exists: AT+OTAUSER declares the
 *  total length up front and then expects exactly that many bytes in
 *  transparent mode, so the length has to be known before the push starts, and
 *  a push that stalls waiting for Wi-Fi would hold the 8711 in transparent mode
 *  with every other AT command locked out behind it.
 *
 *  2 MB, not 1: the vendor's own AT+OTAUSER example declares 1120704 bytes
 *  (~1.07 MB), so a 1 MB buffer would reject a realistically-sized ota_all.bin
 *  at offer time.  Doubling it is free here -- SPIC3 had 4 MB going spare -- and
 *  cheaper than discovering the cap on the bench.  As with the receive cache the
 *  cap is announced up front rather than hit mid-transfer.
 *----------------------------------------------------------------------------*/
#define EB_PSRAM_OTA_STAGE_BASE      (EB_PSRAM_XFER_CACHE_END)
#define EB_PSRAM_OTA_STAGE_SIZE      0x200000u        /* 2 MB */

#define EB_PSRAM_OTA_STAGE_END       (EB_PSRAM_OTA_STAGE_BASE + \
                                      EB_PSRAM_OTA_STAGE_SIZE)

/*----------------------------------------------------------------------------*
 *  Consistency checks
 *
 *  These are the whole point of the file: the numbers above are absolute
 *  addresses spread across a .c, a .h and a .overlay, and absolute addresses
 *  drift silently.
 *----------------------------------------------------------------------------*/

BUILD_ASSERT(EB_PSRAM_STREAM_FRAME_END <= SPIC1_MEM_BASE + EB_PSRAM1_NC_SPAN,
             "the preview frame buffer overflows the non-cacheable MPU region "
             "set up by app_lower_init.c:app_mpu_config()");

BUILD_ASSERT(EB_PSRAM_STREAM_FRAME_BASE >= EB_PSRAM_GUI_LOWER_END,
             "the preview frame buffer overlaps the HoneyGUI lower heap");

BUILD_ASSERT(EB_PSRAM_XFER_CACHE_END <= SPIC3_MEM_BASE + EB_PSRAM3_NC_SPAN,
             "the Wi-Fi receive cache overflows the non-cacheable MPU region "
             "app_lower_init.c:app_mpu_config() maps over SPIC3");

BUILD_ASSERT(EB_PSRAM_OTA_STAGE_BASE >= EB_PSRAM_XFER_CACHE_END,
             "the OTA staging buffer overlaps the Wi-Fi receive cache");

BUILD_ASSERT(EB_PSRAM_OTA_STAGE_END <= SPIC3_MEM_BASE + EB_PSRAM3_NC_SPAN,
             "the OTA staging buffer overflows the non-cacheable MPU region "
             "app_lower_init.c:app_mpu_config() maps over SPIC3 -- shrink "
             "EB_PSRAM_OTA_STAGE_SIZE or give it a region of its own");

#if DT_NODE_EXISTS(DT_NODELABEL(psram1_nc))
/* The snippet is in the build, so the DT node is the authority on where the SPI
 * slot buffers live and these macros must agree with it.  Without the snippet
 * there is no node and nothing to check -- the SPIC1 carve-outs still have to
 * agree with each other, which the asserts above cover. */
BUILD_ASSERT(EB_PSRAM_GUI_LOWER_END <= DT_REG_ADDR(DT_NODELABEL(psram1_nc)),
             "the HoneyGUI lower heap overlaps psram1_nc -- shrink "
             "EB_PSRAM_GUI_LOWER_SIZE, or move the psram1_nc node in "
             "snippets/wifi/wifi.overlay");

/* The preview buffer sits ABOVE psram1_nc, so this is the one assert whose sense
 * is reversed from the heap's -- it must start after the node ENDS, not before
 * it begins. */
BUILD_ASSERT(EB_PSRAM_STREAM_FRAME_BASE >=
             DT_REG_ADDR(DT_NODELABEL(psram1_nc)) +
             DT_REG_SIZE(DT_NODELABEL(psram1_nc)),
             "the preview frame buffer overlaps psram1_nc -- raise "
             "EB_PSRAM_STREAM_FRAME_BASE past the end of that node");

BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(psram1_nc)) +
             DT_REG_SIZE(DT_NODELABEL(psram1_nc)) <=
             SPIC1_MEM_BASE + EB_PSRAM1_NC_SPAN,
             "psram1_nc extends past the non-cacheable MPU region, so its GDMA "
             "buffers would need explicit cache maintenance");
#endif

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PSRAM_MAP_H_ */
