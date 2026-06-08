#ifndef RUSTMCUCLAW_MCU_H
#define RUSTMCUCLAW_MCU_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int rustmcuclaw_mcu_init(uint8_t *heap_ptr,
                         size_t heap_len,
                         const char *data_dir,
                         uint8_t *out,
                         size_t out_cap);

int rustmcuclaw_mcu_handle_line(const char *line,
                                uint8_t *out,
                                size_t out_cap);

int rustmcuclaw_mcu_poll(uint8_t *out, size_t out_cap);

int rustmcuclaw_mcu_persist_session_summary(uint8_t *out, size_t out_cap);

/* Query the persisted Feishu channel credentials so the C bootstrap can
 * auto-issue `ATFL=<app_id>,<app_secret>` to the Z2Plus AT firmware right
 * after `rustmcuclaw_mcu_init`. Returns 1 when the channel is enabled and
 * both credentials are present, 0 when disabled or incomplete, -1 when the
 * Rust engine has not been initialised yet. The output buffers receive
 * null-terminated UTF-8 strings. Any of the buffer pointers may be NULL if
 * the caller does not care about that field. */
int rustmcuclaw_mcu_get_feishu_creds(uint8_t *app_id_out,
                                     size_t app_id_cap,
                                     uint8_t *app_secret_out,
                                     size_t app_secret_cap,
                                     uint8_t *endpoint_out,
                                     size_t endpoint_cap);

/* Consume one Feishu event block emitted by the Z2Plus AT URC stream
 * `[FSEV-BEGIN]` … `[FSEV-END]` (see z2_feishu_ws.c::emit_event_urc).
 * On a text `im.message.receive_v1` event the Rust side runs the same LLM
 * pipeline as the serial CLI and replies via /open-apis/im/v1/messages
 * (which itself goes through `claw_mcu_https_post_json`). `out`/`out_cap`
 * receive a short human-readable status string for the serial log. */
int rustmcuclaw_mcu_handle_feishu_event(const uint8_t *block,
                                        size_t block_len,
                                        uint8_t *out,
                                        size_t out_cap);

/* Heap monitoring: return bytes used / total size of the PSRAM bump allocator.
 * Used to detect heap exhaustion and trigger a proactive reinit before OOM. */
size_t rustmcuclaw_mcu_heap_used(void);
size_t rustmcuclaw_mcu_heap_size(void);

/* Platform callbacks consumed by the Rust static library. */
int64_t claw_mcu_fs_read(const char *path, uint8_t *out, size_t out_cap);
int claw_mcu_fs_write(const char *path, const uint8_t *data, size_t data_len, bool append);
int64_t claw_mcu_fs_list(const char *path, uint8_t *out, size_t out_cap);
int claw_mcu_read_temp_humidity(int32_t *temp_milli_c, int32_t *humidity_milli_pct);

/* Board-direct LED control (driven by GPIO via the `gpio-leds` overlay
 * node `claw_leds`). The firmware owns a fixed table of named LEDs and
 * exposes them to the Rust agent by integer index.
 *
 *   claw_mcu_led_count() -> number of LEDs known to the firmware
 *   claw_mcu_led_name(idx, out, out_cap) -> copy null-terminated label
 *       (e.g. "led1") into `out`. Returns 0 on success, negative on bad
 *       index or insufficient buffer.
 *   claw_mcu_led_set(idx, on) -> 1 = on, 0 = off. Returns 0 on success.
 *   claw_mcu_led_get(idx)  -> 1 if on, 0 if off, negative on error.
 *
 * Index-based addressing keeps the LLM <-> hardware boundary clean: the
 * agent enumerates names via led_list at runtime instead of hard-coding
 * pin numbers in prompts. */
int claw_mcu_led_count(void);
int claw_mcu_led_name(int idx, uint8_t *out, size_t out_cap);
int claw_mcu_led_set(int idx, int on);
int claw_mcu_led_get(int idx);

/* Yield-aware sleep used by the Rust agent for short, non-realtime
 * intervals (e.g. LED blink pacing). Capped on the C side so a buggy LLM
 * call cannot stall the poll loop indefinitely. */
void claw_mcu_sleep_ms(uint32_t ms);

int64_t claw_mcu_https_post_json(const char *endpoint,
                                 const char *api_key,
                                 const uint8_t *body,
                                 size_t body_len,
                                 uint8_t *out,
                                 size_t out_cap);
uint64_t claw_mcu_now_secs(void);
int claw_mcu_display_width(void);
int claw_mcu_display_height(void);
int claw_mcu_display_fill_rgb565(uint16_t color);
int claw_mcu_display_present_rgb565(const uint16_t *pixels,
                                    uint16_t width,
                                    uint16_t height);

/* Draw null-terminated UTF-8 text into a caller-provided RGB565 framebuffer.
 * Uses the GNU Unifont bitmap loaded from SD card into PSRAM when available;
 * falls back to the built-in font8x8 for ASCII-only characters otherwise.
 *
 *  pixels  – pointer to the RGB565 buffer (width × height u16 words, row-major)
 *  width   – buffer width in pixels
 *  height  – buffer height in pixels
 *  x, y   – top-left text origin
 *  text    – null-terminated UTF-8 string (Chinese / Latin / etc.)
 *  fg      – foreground colour in RGB565
 *  scale   – pixel magnification (1 = natural glyph size, 2 = 2×, …)
 *
 * Returns the number of glyphs rendered, or a negative value on bad input.
 */
int rustmcuclaw_mcu_draw_text_rgb565(uint16_t *pixels,
                                     uint16_t width,
                                     uint16_t height,
                                     int x,
                                     int y,
                                     const char *text,
                                     uint16_t fg,
                                     uint8_t scale);

/* Look up a Unicode codepoint in the PSRAM-resident GNU Unifont bitmap.
 * Called by the Rust draw function; safe to call before unifont is loaded
 * (returns 0 in that case).
 *
 *  codepoint – Unicode scalar value (BMP only, U+0000 … U+FFFF)
 *  out_rows  – caller-provided array of 16 uint16_t values; on success each
 *              element holds one row of the glyph bitmap with the leftmost
 *              pixel in bit 15 (MSB-first).
 *
 * Returns glyph width in pixels (8 for halfwidth, 16 for fullwidth) on
 * success, or 0 if the codepoint is not found / unifont not yet loaded.
 */
int claw_mcu_unifont_get_glyph(uint32_t codepoint, uint16_t *out_rows);

#ifdef __cplusplus
}
#endif

#endif /* RUSTMCUCLAW_MCU_H */
