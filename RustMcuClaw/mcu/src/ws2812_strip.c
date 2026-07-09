/*
 * WS2812B RGB LED strip bit-bang driver for RTL87X3G (Cortex-M55).
 *
 * Pin: P2_1 = GPIOA bit 16.  See ws2812_strip.h for the design rationale.
 */

#include "ws2812_strip.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#include "pm.h" /* pm_cpu_freq_get() */

LOG_MODULE_REGISTER(ws2812_strip, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* Pin / register addresses                                           */
/* ------------------------------------------------------------------ */

/* GPIOA base from realtek,rtl87x3g.dtsi (`gpio@40001000`).  The data
 * register is at offset 0x00 - see modules/hal/realtek/rtl87x3g/drivers/
 * inc/rtl876x_gpio_def.h.  Writing GPIO_DR drives all 32 GPIOA pins; we
 * read-modify a cached value so neighbouring pins are not disturbed. */
#define GPIOA_DR_ADDR     0x40001000UL
#define GPIOA_DR          (*(volatile uint32_t *)GPIOA_DR_ADDR)

#define WS2812_GPIO_BIT   16U                 /* P2_1 -> GPIOA16 */
#define WS2812_PIN_MASK   (1U << WS2812_GPIO_BIT)

/* Zephyr GPIO device - used at init time only.  Once the pad is in GPIO
 * SW mode we hit GPIOA_DR directly to make the timing loop predictable. */
#define WS2812_GPIO_DEV   DEVICE_DT_GET(DT_NODELABEL(gpioa))

/* ------------------------------------------------------------------ */
/* WS2812B timing (typ., ±150 ns tolerance per worldsemi datasheet)   */
/* ------------------------------------------------------------------ */

#define WS2812_T0H_NS     400U
#define WS2812_T1H_NS     800U
#define WS2812_BIT_NS     1250U
#define WS2812_RESET_US   80U

/* ------------------------------------------------------------------ */
/* DWT cycle counter (Cortex-M55)                                      */
/* ------------------------------------------------------------------ */

#define DEMCR_ADDR        0xE000EDFCUL
#define DWT_CTRL_ADDR     0xE0001000UL
#define DWT_CYCCNT_ADDR   0xE0001004UL
#define DEMCR             (*(volatile uint32_t *)DEMCR_ADDR)
#define DWT_CTRL          (*(volatile uint32_t *)DWT_CTRL_ADDR)
#define DWT_CYCCNT        (*(volatile uint32_t *)DWT_CYCCNT_ADDR)

#define DEMCR_TRCENA      (1U << 24)
#define DWT_CTRL_CYCCNTENA (1U << 0)

static inline void dwt_enable(void)
{
    DEMCR |= DEMCR_TRCENA;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
}

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */

static bool     s_inited;
static uint32_t s_cyc_t0h;       /* high pulse cycles for a 0-bit */
static uint32_t s_cyc_t1h;       /* high pulse cycles for a 1-bit */
static uint32_t s_cyc_bit;       /* total cycles for a single bit */

/* WS2812B accepts colours in GRB order, MSB first. */
static uint8_t  s_pixels[WS2812_STRIP_LEN][3];

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

int ws2812_strip_init(void)
{
    if (s_inited)
    {
        return 0;
    }

    const struct device *gpio_dev = WS2812_GPIO_DEV;
    if (!device_is_ready(gpio_dev))
    {
        LOG_ERR("GPIOA not ready");
        return -ENODEV;
    }

    /* Drive P2_1 low so the strip sees a defined idle level before we
     * start clocking bits out. */
    int ret = gpio_pin_configure(gpio_dev, WS2812_GPIO_BIT,
                                 GPIO_OUTPUT_LOW);
    if (ret < 0)
    {
        LOG_ERR("gpio_pin_configure(P2_1) failed: %d", ret);
        return ret;
    }

    dwt_enable();

    uint32_t cpu_mhz = pm_cpu_freq_get();
    if (cpu_mhz == 0)
    {
        cpu_mhz = 125U; /* sensible default; matches Realtek's stock PM */
    }
    /* cycles = ns * MHz / 1000 - use 64-bit math to avoid wrap-around. */
    s_cyc_t0h = (uint32_t)(((uint64_t)WS2812_T0H_NS * cpu_mhz) / 1000U);
    s_cyc_t1h = (uint32_t)(((uint64_t)WS2812_T1H_NS * cpu_mhz) / 1000U);
    s_cyc_bit = (uint32_t)(((uint64_t)WS2812_BIT_NS * cpu_mhz) / 1000U);

    memset(s_pixels, 0, sizeof(s_pixels));
    s_inited = true;

    LOG_INF("WS2812B strip ready: %u LEDs on P2_1, cpu=%u MHz "
            "(t0h=%u t1h=%u bit=%u cyc)",
            (unsigned)WS2812_STRIP_LEN, (unsigned)cpu_mhz,
            (unsigned)s_cyc_t0h, (unsigned)s_cyc_t1h,
            (unsigned)s_cyc_bit);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Back-buffer helpers                                                 */
/* ------------------------------------------------------------------ */

int ws2812_strip_set_pixel(uint32_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx >= WS2812_STRIP_LEN)
    {
        return -EINVAL;
    }
    s_pixels[idx][0] = g;
    s_pixels[idx][1] = r;
    s_pixels[idx][2] = b;
    return 0;
}

void ws2812_strip_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t i = 0; i < WS2812_STRIP_LEN; i++)
    {
        s_pixels[i][0] = g;
        s_pixels[i][1] = r;
        s_pixels[i][2] = b;
    }
}

int ws2812_strip_clear(void)
{
    ws2812_strip_fill(0, 0, 0);
    return ws2812_strip_show();
}

/* ------------------------------------------------------------------ */
/* Transmission                                                        */
/* ------------------------------------------------------------------ */

/* Push one byte MSB-first onto the wire.  Must be called with IRQs off
 * and the DWT cycle counter enabled. */
static __attribute__((always_inline)) inline
void ws2812_send_byte(uint8_t byte,
                      uint32_t dr_high,
                      uint32_t dr_low,
                      uint32_t cyc_t0h,
                      uint32_t cyc_t1h,
                      uint32_t cyc_bit)
{
    for (uint32_t mask = 0x80U; mask != 0U; mask >>= 1)
    {
        uint32_t start = DWT_CYCCNT;
        uint32_t high_cycles = (byte & mask) ? cyc_t1h : cyc_t0h;

        GPIOA_DR = dr_high;
        while ((DWT_CYCCNT - start) < high_cycles)
        {
            /* spin */
        }
        GPIOA_DR = dr_low;
        while ((DWT_CYCCNT - start) < cyc_bit)
        {
            /* spin */
        }
    }
}

int ws2812_strip_show(void)
{
    if (!s_inited)
    {
        int ret = ws2812_strip_init();
        if (ret < 0)
        {
            return ret;
        }
    }

    const uint8_t *buf = (const uint8_t *)s_pixels;
    const size_t   len = sizeof(s_pixels);

    /* Snapshot the current GPIOA_DR with our bit forced high / low so
     * the timing loop is just two stores; neighbouring pins are
     * preserved (IRQs are locked, so nothing else touches the
     * register). */
    unsigned int key = irq_lock();

    uint32_t dr_now  = GPIOA_DR;
    uint32_t dr_high = dr_now | WS2812_PIN_MASK;
    uint32_t dr_low  = dr_now & ~WS2812_PIN_MASK;

    /* Ensure the line starts low for the reset gap. */
    GPIOA_DR = dr_low;

    const uint32_t cyc_t0h = s_cyc_t0h;
    const uint32_t cyc_t1h = s_cyc_t1h;
    const uint32_t cyc_bit = s_cyc_bit;

    for (size_t i = 0; i < len; i++)
    {
        ws2812_send_byte(buf[i], dr_high, dr_low,
                         cyc_t0h, cyc_t1h, cyc_bit);
    }

    /* Guarantee the line is idle low before we re-open IRQs. */
    GPIOA_DR = dr_low;

    irq_unlock(key);

    /* Latch: WS2812B requires at least 50 us of low to commit the frame. */
    k_busy_wait(WS2812_RESET_US);

    return 0;
}

/* ------------------------------------------------------------------ */
/* HSV -> RGB (used by both the boot test and the rainbow effect)      */
/* ------------------------------------------------------------------ */

static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t  region = (uint8_t)((h / 60U) % 6U);
    uint16_t f      = (uint16_t)(h - (uint16_t)(region * 60U)); /* 0..59 */
    uint8_t  p = (uint8_t)((uint16_t)v * (uint16_t)(255U - s) / 255U);
    uint8_t  q = (uint8_t)((uint32_t)v *
                           (255U - (uint32_t)s * f / 60U) / 255U);
    uint8_t  t = (uint8_t)((uint32_t)v *
                           (255U - (uint32_t)s * (60U - f) / 60U) / 255U);

    switch (region)
    {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

int ws2812_strip_boot_test(void)
{
    int ret = ws2812_strip_init();
    if (ret < 0)
    {
        return ret;
    }

    LOG_INF("WS2812B boot test: solid colour sweep");

    /* Solid red / green / blue at modest brightness (~25%) so the
     * 5 V rail does not droop while the firmware is still booting. */
    static const uint8_t colours[][3] =
    {
        {64, 0, 0},
        {0, 64, 0},
        {0, 0, 64},
    };
    for (size_t i = 0; i < ARRAY_SIZE(colours); i++)
    {
        ws2812_strip_fill(colours[i][0], colours[i][1], colours[i][2]);
        ws2812_strip_show();
        k_msleep(300);
    }

    LOG_INF("WS2812B boot test: rainbow sweep");
    for (uint8_t step = 0; step < 30U; step++)
    {
        for (uint32_t i = 0; i < WS2812_STRIP_LEN; i++)
        {
            uint16_t hue = (uint16_t)(((i * 360U) / WS2812_STRIP_LEN
                                       + (uint32_t)step * 12U) % 360U);
            uint8_t r, g, b;
            hsv_to_rgb(hue, 255, 48, &r, &g, &b);
            ws2812_strip_set_pixel(i, r, g, b);
        }
        ws2812_strip_show();
        k_msleep(40);
    }

    /* Leave the strip off after the test so it does not draw current
     * during normal operation. */
    return ws2812_strip_clear();
}

/* ------------------------------------------------------------------ */
/* Non-blocking effect engine                                          */
/* ------------------------------------------------------------------ */
/*
 * A single `k_work_delayable` re-schedules itself on the system
 * workqueue.  State is guarded by a mutex because callers can come from
 * the CLI thread, the Rust LLM thread, etc.  Re-entering an effect call
 * cancels the previous one and replaces the parameters atomically.
 */

typedef enum
{
    WS2812_FX_NONE = 0,
    WS2812_FX_SOLID,
    WS2812_FX_BLINK,
    WS2812_FX_RAINBOW,
    WS2812_FX_PIXELS,
} ws2812_fx_kind_t;

static struct k_mutex          s_fx_lock;
static struct k_work_delayable s_fx_work;
static bool                    s_fx_inited;

static ws2812_fx_kind_t s_fx_kind;
static uint8_t          s_fx_r, s_fx_g, s_fx_b;
static uint8_t          s_fx_value;          /* HSV value for rainbow */
static uint32_t         s_fx_frame_ms;
static int64_t          s_fx_end_uptime;     /* -1 = forever */
static uint32_t         s_fx_token;          /* bumped on every restart */
static uint32_t         s_fx_frame_idx;
static uint8_t          s_fx_pixels[WS2812_STRIP_LEN][3]; /* RGB snapshot */

/* Frame period used by the static effects (solid / pixels) just to
 * re-check the expiry deadline.  Low resolution is fine here. */
#define WS2812_FX_KEEPALIVE_MS 50U

static void ws2812_fx_apply_pixels_locked(void)
{
    for (uint32_t i = 0; i < WS2812_STRIP_LEN; i++)
    {
        ws2812_strip_set_pixel(i,
                               s_fx_pixels[i][0],
                               s_fx_pixels[i][1],
                               s_fx_pixels[i][2]);
    }
}

static void ws2812_fx_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    /* Snapshot state under the lock; rendering itself does not need
     * the lock because the back buffer is owned by this work. */
    k_mutex_lock(&s_fx_lock, K_FOREVER);

    ws2812_fx_kind_t kind = s_fx_kind;
    uint32_t token        = s_fx_token;
    int64_t  end_uptime   = s_fx_end_uptime;
    uint8_t  r = s_fx_r, g = s_fx_g, b = s_fx_b;
    uint8_t  value        = s_fx_value;
    uint32_t frame_ms     = s_fx_frame_ms;
    uint32_t frame_idx    = s_fx_frame_idx;

    k_mutex_unlock(&s_fx_lock);

    if (kind == WS2812_FX_NONE)
    {
        return;
    }

    int64_t  now     = k_uptime_get();
    bool     expired = (end_uptime >= 0) && (now >= end_uptime);

    if (expired)
    {
        (void)ws2812_strip_clear();
        k_mutex_lock(&s_fx_lock, K_FOREVER);
        /* Only clear state if we are still the active token - a
         * concurrent restart may already have queued a fresh effect. */
        if (s_fx_token == token)
        {
            s_fx_kind = WS2812_FX_NONE;
        }
        k_mutex_unlock(&s_fx_lock);
        return;
    }

    uint32_t next_delay = frame_ms;

    switch (kind)
    {
    case WS2812_FX_SOLID:
        if (frame_idx == 0U)
        {
            ws2812_strip_fill(r, g, b);
            (void)ws2812_strip_show();
        }
        next_delay = WS2812_FX_KEEPALIVE_MS;
        break;

    case WS2812_FX_PIXELS:
        if (frame_idx == 0U)
        {
            k_mutex_lock(&s_fx_lock, K_FOREVER);
            ws2812_fx_apply_pixels_locked();
            k_mutex_unlock(&s_fx_lock);
            (void)ws2812_strip_show();
        }
        next_delay = WS2812_FX_KEEPALIVE_MS;
        break;

    case WS2812_FX_BLINK:
        if ((frame_idx & 1U) == 0U)
        {
            ws2812_strip_fill(r, g, b);
        }
        else
        {
            ws2812_strip_fill(0, 0, 0);
        }
        (void)ws2812_strip_show();
        break;

    case WS2812_FX_RAINBOW:
        for (uint32_t i = 0; i < WS2812_STRIP_LEN; i++)
        {
            uint16_t hue = (uint16_t)(((i * 360U) / WS2812_STRIP_LEN
                                       + frame_idx * 6U) % 360U);
            uint8_t rr, gg, bb;
            hsv_to_rgb(hue, 255, value, &rr, &gg, &bb);
            ws2812_strip_set_pixel(i, rr, gg, bb);
        }
        (void)ws2812_strip_show();
        break;

    default:
        return;
    }

    /* Bound the next-frame delay by the remaining duration so the
     * strip turns off promptly when the deadline elapses. */
    if (end_uptime >= 0)
    {
        int64_t remain = end_uptime - now;
        if (remain <= 0)
        {
            (void)ws2812_strip_clear();
            k_mutex_lock(&s_fx_lock, K_FOREVER);
            if (s_fx_token == token)
            {
                s_fx_kind = WS2812_FX_NONE;
            }
            k_mutex_unlock(&s_fx_lock);
            return;
        }
        if ((uint32_t)remain < next_delay)
        {
            next_delay = (uint32_t)remain;
        }
    }

    k_mutex_lock(&s_fx_lock, K_FOREVER);
    /* If another caller swapped the effect while we were rendering,
     * abandon this branch - that caller has already queued its own
     * first frame. */
    if (s_fx_token == token && s_fx_kind == kind)
    {
        s_fx_frame_idx = frame_idx + 1U;
        k_work_schedule(&s_fx_work, K_MSEC(next_delay));
    }
    k_mutex_unlock(&s_fx_lock);
}

static int ws2812_fx_ensure_init(void)
{
    if (!s_fx_inited)
    {
        k_mutex_init(&s_fx_lock);
        k_work_init_delayable(&s_fx_work, ws2812_fx_work_handler);
        s_fx_inited = true;
    }
    return ws2812_strip_init();
}

/* Common path: cancel the previous effect, install new state, kick the
 * first frame immediately. */
static int ws2812_fx_restart_locked(ws2812_fx_kind_t kind,
                                    uint32_t frame_ms,
                                    uint32_t duration_ms)
{
    s_fx_kind        = kind;
    s_fx_frame_ms    = frame_ms ? frame_ms : WS2812_FX_KEEPALIVE_MS;
    s_fx_frame_idx   = 0U;
    s_fx_end_uptime  = duration_ms ? (k_uptime_get() + duration_ms) : -1;
    s_fx_token++;
    return 0;
}

int ws2812_strip_effect_stop(void)
{
    int ret = ws2812_fx_ensure_init();
    if (ret < 0)
    {
        return ret;
    }

    k_mutex_lock(&s_fx_lock, K_FOREVER);
    s_fx_kind = WS2812_FX_NONE;
    s_fx_token++;
    k_mutex_unlock(&s_fx_lock);

    /* `cancel_sync` waits for any in-flight handler so the call is
     * race-free w.r.t. the final `strip_clear()` below. */
    (void)k_work_cancel_delayable_sync(&s_fx_work,
    &(struct k_work_sync) {0});
    return ws2812_strip_clear();
}

int ws2812_strip_effect_solid(uint8_t r, uint8_t g, uint8_t b,
                              uint32_t duration_ms)
{
    int ret = ws2812_fx_ensure_init();
    if (ret < 0)
    {
        return ret;
    }

    k_mutex_lock(&s_fx_lock, K_FOREVER);
    s_fx_r = r; s_fx_g = g; s_fx_b = b;
    ws2812_fx_restart_locked(WS2812_FX_SOLID, 0U, duration_ms);
    k_mutex_unlock(&s_fx_lock);

    return k_work_reschedule(&s_fx_work, K_NO_WAIT);
}

int ws2812_strip_effect_blink(uint8_t r, uint8_t g, uint8_t b,
                              uint32_t period_ms, uint32_t duration_ms)
{
    int ret = ws2812_fx_ensure_init();
    if (ret < 0)
    {
        return ret;
    }
    if (period_ms < 20U)
    {
        period_ms = 20U;
    }

    k_mutex_lock(&s_fx_lock, K_FOREVER);
    s_fx_r = r; s_fx_g = g; s_fx_b = b;
    /* The handler toggles on every wake-up, so each half-period needs
     * its own frame. */
    ws2812_fx_restart_locked(WS2812_FX_BLINK, period_ms / 2U, duration_ms);
    k_mutex_unlock(&s_fx_lock);

    return k_work_reschedule(&s_fx_work, K_NO_WAIT);
}

int ws2812_strip_effect_rainbow(uint8_t value, uint32_t frame_ms,
                                uint32_t duration_ms)
{
    int ret = ws2812_fx_ensure_init();
    if (ret < 0)
    {
        return ret;
    }
    if (frame_ms < 10U)
    {
        frame_ms = 10U;
    }

    k_mutex_lock(&s_fx_lock, K_FOREVER);
    s_fx_value = value;
    ws2812_fx_restart_locked(WS2812_FX_RAINBOW, frame_ms, duration_ms);
    k_mutex_unlock(&s_fx_lock);

    return k_work_reschedule(&s_fx_work, K_NO_WAIT);
}

int ws2812_strip_effect_pixels(const uint8_t *rgb, uint32_t length,
                               uint32_t duration_ms)
{
    if (rgb == NULL || length == 0U)
    {
        return -EINVAL;
    }

    int ret = ws2812_fx_ensure_init();
    if (ret < 0)
    {
        return ret;
    }

    uint32_t n = (length < WS2812_STRIP_LEN) ? length : WS2812_STRIP_LEN;

    k_mutex_lock(&s_fx_lock, K_FOREVER);
    memset(s_fx_pixels, 0, sizeof(s_fx_pixels));
    for (uint32_t i = 0; i < n; i++)
    {
        s_fx_pixels[i][0] = rgb[i * 3U + 0U];
        s_fx_pixels[i][1] = rgb[i * 3U + 1U];
        s_fx_pixels[i][2] = rgb[i * 3U + 2U];
    }
    ws2812_fx_restart_locked(WS2812_FX_PIXELS, 0U, duration_ms);
    k_mutex_unlock(&s_fx_lock);

    return k_work_reschedule(&s_fx_work, K_NO_WAIT);
}
