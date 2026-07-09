/*
 * WS2812B RGB LED strip driver (bit-banged) for RTL87X3G.
 *
 * Hardware:
 *   - 30-LED WS2812B strip with data-in tied to P2_1 (GPIOA bit 16).
 *   - 5 V supply, common ground with the MCU board.
 *
 * Implementation notes:
 *   - Zephyr's stock `ws2812_gpio` driver is hard-coded for nRF51 inline
 *     assembly and the `ws2812_spi` driver would require routing one of
 *     the RTL87X3G SPI peripherals to P2_1 with a 6.4 MHz MOSI clock.
 *     A direct GPIO bit-bang using the Cortex-M55's DWT cycle counter is
 *     simpler and keeps the rest of the board wiring untouched.
 *   - Transmission runs with IRQs locked.  Worst case for 30 LEDs is
 *     30 * 24 bits * 1.25 us = 900 us, well below the system tick.
 */

#ifndef RUSTMCUCLAW_WS2812_STRIP_H_
#define RUSTMCUCLAW_WS2812_STRIP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WS2812_STRIP_LEN
#define WS2812_STRIP_LEN 30
#endif

/**
 * @brief Configure P2_1 as a push-pull GPIO output and cache CPU-cycle
 *        timing constants for the bit-bang loop.
 *
 * Idempotent.  Returns 0 on success or a negative errno.
 */
int ws2812_strip_init(void);

/**
 * @brief Set a single pixel in the back buffer.
 *
 * The change is not visible until @ref ws2812_strip_show is called.
 */
int ws2812_strip_set_pixel(uint32_t idx, uint8_t r, uint8_t g, uint8_t b);

/** Fill every pixel in the back buffer with the same colour. */
void ws2812_strip_fill(uint8_t r, uint8_t g, uint8_t b);

/** Shorthand for `ws2812_strip_fill(0, 0, 0); ws2812_strip_show()`. */
int ws2812_strip_clear(void);

/**
 * @brief Push the current back buffer to the strip.
 *
 * Locks IRQs for the duration of the transmission (~1 ms for 30 LEDs)
 * and then waits the WS2812B latch time before returning.
 */
int ws2812_strip_show(void);

/**
 * @brief Boot-time self-test: red/green/blue/rainbow sweep, then clear.
 *
 * Intended to be called once from `main()` so the operator can confirm
 * the strip is wired and powered correctly.
 */
int ws2812_strip_boot_test(void);

/* ------------------------------------------------------------------ */
/* Non-blocking effect engine                                          */
/* ------------------------------------------------------------------ */
/*
 * All `ws2812_strip_effect_*` calls return immediately.  Rendering is
 * driven from the system workqueue via a `k_work_delayable`, so the
 * caller (main thread, CLI, Rust agent, ...) is never blocked.
 *
 * Calling any effect function cancels and replaces the currently
 * running effect; `ws2812_strip_effect_stop()` also clears the strip.
 *
 * `duration_ms = 0` means "run forever, until stopped or replaced".
 * Any positive value clears the strip when the time elapses.
 */

/** Cancel the running effect and turn every LED off. */
int ws2812_strip_effect_stop(void);

/**
 * @brief Solid colour for `duration_ms` (0 = until stopped).
 */
int ws2812_strip_effect_solid(uint8_t r, uint8_t g, uint8_t b,
                              uint32_t duration_ms);

/**
 * @brief Toggle the whole strip between (r,g,b) and off every
 *        `period_ms / 2` ms.  `duration_ms = 0` = forever.
 */
int ws2812_strip_effect_blink(uint8_t r, uint8_t g, uint8_t b,
                              uint32_t period_ms, uint32_t duration_ms);

/**
 * @brief Animated rainbow sweep.
 *
 * @param value         HSV value/brightness 0..255.  60 is a safe default
 *                      that does not stress the 5 V rail.
 * @param frame_ms      Time between frames (lower = faster).  Min 10 ms.
 * @param duration_ms   Total runtime; 0 = forever.
 */
int ws2812_strip_effect_rainbow(uint8_t value, uint32_t frame_ms,
                                uint32_t duration_ms);

/**
 * @brief Show a caller-provided frame (per-LED colours).
 *
 * @param rgb       Pointer to an `WS2812_STRIP_LEN x 3` byte array in
 *                  R,G,B order.  The buffer is copied internally, so
 *                  the caller can free / reuse it after returning.
 * @param length    Number of pixels in `rgb`.  Must equal
 *                  WS2812_STRIP_LEN (extra LEDs would remain dark).
 * @param duration_ms  How long to display before clearing; 0 = forever.
 */
int ws2812_strip_effect_pixels(const uint8_t *rgb, uint32_t length,
                               uint32_t duration_ms);
// /* WS2812B RGB strip self-test on P2_1 - runs before the heavy PSRAM /
//  * display / Rust engine init so the operator gets immediate visual
//  * confirmation that the board booted and the strip is wired right. */
// ret = ws2812_strip_boot_test();
// if (ret < 0) {
//     LOG_WRN("WS2812B boot test failed: %d", ret);
// }

// /* ----------------------------------------------------------------
//  * Non-blocking effect engine verification.
//  * Each effect is started immediately (returns at once) and the
//  * duration parameter auto-clears the strip when time elapses.
//  * We sleep between launches only so the effects are humanly
//  * distinguishable; main execution continues during each sleep.
//  * ---------------------------------------------------------------- */
// LOG_INF("WS2812 effect test: solid red 1 s");
// ws2812_strip_effect_solid(200, 0, 0, 1000);
// k_msleep(1200);

// LOG_INF("WS2812 effect test: solid green 1 s");
// ws2812_strip_effect_solid(0, 200, 0, 1000);
// k_msleep(1200);

// LOG_INF("WS2812 effect test: blink blue 2 s (400 ms period)");
// ws2812_strip_effect_blink(0, 60, 200, 400, 2000);
// k_msleep(2200);

// /* Custom per-pixel frame: first 10 red, next 10 green, last 10 blue */
// LOG_INF("WS2812 effect test: custom pixels 1.5 s");
// {
//     static uint8_t frame[WS2812_STRIP_LEN * 3];
//     for (int i = 0; i < 10; i++) {
//         frame[i*3+0]=180; frame[i*3+1]=0;   frame[i*3+2]=0;
//     }
//     for (int i = 10; i < 20; i++) {
//         frame[i*3+0]=0;   frame[i*3+1]=180; frame[i*3+2]=0;
//     }
//     for (int i = 20; i < 30; i++) {
//         frame[i*3+0]=0;   frame[i*3+1]=0;   frame[i*3+2]=180;
//     }
//     ws2812_strip_effect_pixels(frame, WS2812_STRIP_LEN, 1500);
// }
// k_msleep(1700);

// /* Rainbow runs continuously (duration_ms = 0) from here on; the
//  * Rust agent or CLI can replace it with another effect at any time. */
// LOG_INF("WS2812 effect test: rainbow (continuous)");
// ws2812_strip_effect_rainbow(100, 20, 0);
#ifdef __cplusplus
}
#endif

#endif /* RUSTMCUCLAW_WS2812_STRIP_H_ */
