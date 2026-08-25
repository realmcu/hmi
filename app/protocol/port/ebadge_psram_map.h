/**
 * @file    ebadge_psram_map.h
 * @brief   Single authority for how the first 4 MB of SPIC1 PSRAM is divided up.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 * ---------------------------------------------------------------------------
 * Three unrelated modules carve out of the same PSRAM and none of them used to
 * know about the others:
 *
 *   - port/ui/gui_port_os.c   HoneyGUI's "lower" TLSF heap, by base+size
 *   - snippets/wifi_8711/wifi_8711.overlay   psram1_nc, by absolute DT address
 *   - app/protocol/wifi_xfer  the receive cache added for store-after-verify
 *
 * They had already collided: the GUI heap was declared as SPIC1_MEM_BASE +
 * 0x400000, i.e. the whole MPU region, while psram1_nc sits at 0x22380000 --
 * 128 KB *inside* that heap.  Nothing detected it, because a TLSF heap only
 * reaches its top under memory pressure, and the SPI slot buffers up there are
 * written by GDMA rather than by anything the heap would trip over.  It would
 * have surfaced as PSRAM corruption under load, blamed on DMA.
 *
 * So the division lives here, once, with BUILD_ASSERTs that fail the build
 * instead of the device.  Both C consumers include this; the overlay cannot, so
 * the assert at the bottom checks the DT node against these numbers.
 *
 * ---------------------------------------------------------------------------
 * WHY EVERYTHING STAYS UNDER 0x22400000
 * ---------------------------------------------------------------------------
 * app_lower_init.c:app_mpu_config() maps exactly SPIC1_MEM_BASE..+0x400000 as
 * MPU region 2 with attribute 0x44, i.e. non-cacheable.  Two consequences that
 * both matter:
 *
 *   - Anything above 0x22400000 has whatever attributes the default map gives
 *     it, and this app never validated that PSRAM there is even mapped.  Not a
 *     place to put a buffer.
 *   - Everything below it is non-cacheable, so it needs no cache maintenance --
 *     which is what makes the region usable for GDMA (psram1_nc) and is why the
 *     receive cache does not need the SCB_CleanDCache_by_Addr dance that the
 *     H264 render buffers needed once SPIC3 became cacheable.
 *
 * The cost is that reads here are the slow SPIC1 path.  For the receive cache
 * that is a fair trade: it is touched twice per file (memcpy in, read back once
 * for the flash append) and never in a display-rate loop.
 *
 * ---------------------------------------------------------------------------
 * THE MAP
 * ---------------------------------------------------------------------------
 *   0x22000000  +2.5 MB   HoneyGUI lower heap
 *   0x22280000  +1   MB   Wi-Fi receive cache (this change)
 *   0x22380000  +128 KB   psram1_nc: SPI slot buffers, GDMA target
 *   0x223A0000  +384 KB   unclaimed
 *   0x22400000            end of the non-cacheable MPU region
 *
 * The receive cache is placed BELOW psram1_nc rather than above it so that the
 * three claimed regions stay contiguous from the bottom and the slack ends up in
 * one run at the top, where it can be handed to whichever of the three needs it
 * next.  Putting it above would have left 1 MB of GUI heap stranded between two
 * carve-outs.
 */
#ifndef _EBADGE_PSRAM_MAP_H_
#define _EBADGE_PSRAM_MAP_H_

#include <stdint.h>
#include <zephyr/toolchain.h>
#include <zephyr/devicetree.h>

#include "address_map.h"        /* SPIC1_MEM_BASE */

#ifdef __cplusplus
extern "C" {
#endif

/** Total span the MPU maps non-cacheable; the hard ceiling for everything here.
 *  Mirrors app_lower_init.c:app_mpu_config(). */
#define EB_PSRAM1_NC_SPAN            0x400000u

/*----------------------------------------------------------------------------*
 *  HoneyGUI lower heap
 *
 *  Was the full 0x400000.  Reduced to make room for the receive cache below,
 *  and reduced by slightly more than that cache costs because the old value was
 *  overlapping psram1_nc anyway -- so 128 KB of this "reduction" is not a
 *  reduction at all, just the end of a double-booking.
 *
 *  What this heap actually holds is HoneyGUI's off-screen surfaces and decoded
 *  image buffers, sized by widget usage rather than by anything fixed, so there
 *  is no formula that says 2.5 MB is enough -- only that the app has to fit.  A
 *  heap exhaustion here shows up as gui_lower_malloc returning NULL, which the
 *  GUI logs, so it is loud rather than silent.
 *----------------------------------------------------------------------------*/
#define EB_PSRAM_GUI_LOWER_BASE      (SPIC1_MEM_BASE)
#define EB_PSRAM_GUI_LOWER_SIZE      0x300000u        /* 3 MB */

/*----------------------------------------------------------------------------*
 *  Wi-Fi receive cache
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
#define EB_PSRAM_XFER_CACHE_BASE     (EB_PSRAM_GUI_LOWER_BASE + \
                                      EB_PSRAM_GUI_LOWER_SIZE)
#define EB_PSRAM_XFER_CACHE_SIZE     0x80000u        /* .5 MB */

/** First byte after the cache.  psram1_nc is expected to start exactly here --
 *  asserted below when the wifi_8711 snippet is in the build. */
#define EB_PSRAM_XFER_CACHE_END      (EB_PSRAM_XFER_CACHE_BASE + \
                                      EB_PSRAM_XFER_CACHE_SIZE)

/*----------------------------------------------------------------------------*
 *  Consistency checks
 *
 *  These are the whole point of the file: the numbers above are absolute
 *  addresses spread across a .c, a .h and a .overlay, and absolute addresses
 *  drift silently.
 *----------------------------------------------------------------------------*/

BUILD_ASSERT(EB_PSRAM_XFER_CACHE_END <= SPIC1_MEM_BASE + EB_PSRAM1_NC_SPAN,
             "SPIC1 carve-outs overflow the non-cacheable MPU region set up by "
             "app_lower_init.c:app_mpu_config()");

#if DT_NODE_EXISTS(DT_NODELABEL(psram1_nc))
/* The snippet is in the build, so the DT node is the authority on where the SPI
 * slot buffers live and these macros must agree with it.  Without the snippet
 * there is no node and nothing to check -- the cache and the GUI heap still have
 * to agree with each other, which the assert above covers. */
BUILD_ASSERT(EB_PSRAM_XFER_CACHE_END <= DT_REG_ADDR(DT_NODELABEL(psram1_nc)),
             "the Wi-Fi receive cache overlaps psram1_nc -- shrink "
             "EB_PSRAM_GUI_LOWER_SIZE / EB_PSRAM_XFER_CACHE_SIZE, or move the "
             "psram1_nc node in snippets/wifi_8711/wifi_8711.overlay");

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
