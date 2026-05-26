#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/cache.h>
#include <ff.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pm.h"
#include "wdg.h"
#include "psram_init.h"
#include "rustmcuclaw_mcu.h"
#include "lcd_sh8601z_410_502_qspi.h"

#if DT_NODE_HAS_STATUS(DT_NODELABEL(touch_device), okay)
#include "touch_CHSC6417_zephyr.h"
#define HAVE_TOUCH 1
#else
#define HAVE_TOUCH 0
#endif

LOG_MODULE_REGISTER(rustmcuclaw_mcu, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(uart2), okay)
#define CLI_UART_NODE DT_ALIAS(uart2)
#elif DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
#define CLI_UART_NODE DT_CHOSEN(zephyr_console)
#elif DT_NODE_HAS_STATUS(DT_ALIAS(uart3), okay)
#define CLI_UART_NODE DT_ALIAS(uart3)
#else
#error "RustMcuClaw MCU requires alias uart2/uart3 or chosen zephyr,console"
#endif

#ifndef RUSTMCUCLAW_FS_MOUNT_POINT
#define RUSTMCUCLAW_FS_MOUNT_POINT "/SD:"
#endif

#ifndef RUSTMCUCLAW_DATA_DIR
#define RUSTMCUCLAW_DATA_DIR RUSTMCUCLAW_FS_MOUNT_POINT "/RustMcuClaw"
#endif

#ifndef RUSTMCUCLAW_PSRAM_BASE
#define RUSTMCUCLAW_PSRAM_BASE 0x22000000UL
#endif

#ifndef RUSTMCUCLAW_PSRAM_SIZE
#define RUSTMCUCLAW_PSRAM_SIZE (4UL * 1024UL * 1024UL)
#endif

#ifndef RUSTMCUCLAW_UNIX_EPOCH_OFFSET
#define RUSTMCUCLAW_UNIX_EPOCH_OFFSET 1735689600ULL /* 2025-01-01T00:00:00Z */
#endif

/* Wall-clock baseline established by an SNTP sync via the Z2Plus net card.
 * `claw_mcu_now_secs()` returns this base + (uptime - base_uptime) when set. */
#ifndef ATEP_DEFAULT_HOST
#define ATEP_DEFAULT_HOST "cn.pool.ntp.org"
#endif
#ifndef ATEP_TIMEOUT_MS
#define ATEP_TIMEOUT_MS 15000
#endif
#ifndef ATEP_SNTP_TIMEOUT_S
/* Per-server SNTP wait inside the Z2 firmware. Keep short so we can rotate. */
#define ATEP_SNTP_TIMEOUT_S 5
#endif
/* Rotation list - tried in order on successive boot retries. The LAN gateway
 * is tried first because on Windows ICS shared networks (192.168.137.0/24)
 * outbound UDP/123 is often blocked, but the host PC itself can answer NTP
 * when the Windows Time service is enabled. After that we fall back to public
 * pools in case the device is on a regular router. */
static const char *const k_atep_hosts[] =
{

    "time.windows.com",
    "cn.pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com",
    "pool.ntp.org",
};
static uint64_t wall_clock_base_secs = 0;
static int64_t  wall_clock_base_uptime_ms = 0;
static bool     wall_clock_synced = false;

#if DT_NODE_EXISTS(DT_NODELABEL(psram1_for_mcu))
#define RUSTMCUCLAW_FB_REGION_BASE DT_REG_ADDR(DT_NODELABEL(psram1_for_mcu))
#define RUSTMCUCLAW_FB_REGION_SIZE DT_REG_SIZE(DT_NODELABEL(psram1_for_mcu))
#else
#define RUSTMCUCLAW_FB_REGION_BASE DT_REG_ADDR(DT_NODELABEL(psram0_for_mcu))
#define RUSTMCUCLAW_FB_REGION_SIZE DT_REG_SIZE(DT_NODELABEL(psram0_for_mcu))
#endif

#define RUSTMCUCLAW_FB_GUARD_BYTES (64U * 1024U)
#define RUSTMCUCLAW_FB_BYTES ((uint32_t)(SH8601Z_LCD_WIDTH * SH8601Z_LCD_HEIGHT * sizeof(uint16_t)))
#define RUSTMCUCLAW_FB_BASE ((RUSTMCUCLAW_FB_REGION_BASE + RUSTMCUCLAW_FB_REGION_SIZE) - RUSTMCUCLAW_FB_BYTES)

#ifndef RUSTMCUCLAW_BOOT_CFG_PATH
#define RUSTMCUCLAW_BOOT_CFG_PATH RUSTMCUCLAW_DATA_DIR "/rmcc.toml"
#endif

#ifndef RUSTMCUCLAW_BOOT_PROVIDER_KIND
#define RUSTMCUCLAW_BOOT_PROVIDER_KIND "open_ai_compatible"
#endif

#ifndef RUSTMCUCLAW_BOOT_PROVIDER_MODEL
#define RUSTMCUCLAW_BOOT_PROVIDER_MODEL KIMI_MODEL
#endif

#ifndef RUSTMCUCLAW_BOOT_PROVIDER_ENDPOINT
#define RUSTMCUCLAW_BOOT_PROVIDER_ENDPOINT KIMI_ENDPOINT
#endif

#ifndef RUSTMCUCLAW_BOOT_PROVIDER_API_KEY
#define RUSTMCUCLAW_BOOT_PROVIDER_API_KEY KIMI_API_KEY
#endif

#define RUSTMCUCLAW_BOOT_TOML \
    "data_dir = \"" RUSTMCUCLAW_DATA_DIR "\"\n\n" \
    "[provider]\n" \
    "kind = \"" RUSTMCUCLAW_BOOT_PROVIDER_KIND "\"\n" \
    "model = \"" RUSTMCUCLAW_BOOT_PROVIDER_MODEL "\"\n" \
    "endpoint = \"" RUSTMCUCLAW_BOOT_PROVIDER_ENDPOINT "\"\n" \
    "api_key = \"" RUSTMCUCLAW_BOOT_PROVIDER_API_KEY "\"\n" \
    "temperature = 0.2\n" \
    "max_tokens = 2048\n\n" \
    "[chat]\n" \
    "history_limit = 6\n" \
    "max_tool_loops = 4\n\n" \
    "[heartbeat]\n" \
    "enabled = true\n" \
    "interval_secs = 60\n\n" \
    "[files]\n" \
    "soul = \"soul.md\"\n" \
    "user = \"user.md\"\n" \
    "role = \"role.md\"\n" \
    "summary_memory = \"summary_memory.jsonl\"\n" \
    "tasks = \"tasks.json\"\n"

#define CLI_LINE_MAX 1024
#define CLAW_OUT_MAX (24 * 1024)
#define CLI_STACK_SIZE 12288
#define POLL_STACK_SIZE 4096
/* Feishu URC reader: parses [FSEV-BEGIN]...[FSEV-END] blocks pushed by Z2Plus
 * and dispatches them to the Rust LLM pipeline. Stack is sized for the
 * 16 KB scratch buffer pointer + JSON parsing path inside Rust. */
#define Z2_URC_STACK_SIZE 4096
#define CLI_PRIORITY 5
#define POLL_PRIORITY 6
/* Lower priority (higher numeric value) than CLI so chat replies are never
 * starved by an incoming Feishu push that triggers its own LLM round-trip. */
#define Z2_URC_PRIORITY 7
#define FSEV_BUF_MAX (12 * 1024)
#define Z2_HOST_MAX 128
#define Z2_PATH_MAX 256
#define Z2_STATUS_LINE_MAX 512
#define Z2_HTTPS_TIMEOUT_MS 190000
#define CLI_UART_RX_QUEUE_LEN 2048
#define Z2_UART_RX_QUEUE_LEN 2048
#define Z2_AT_CHUNK_SIZE 1024
#define Z2_AT_CHUNK_THROTTLE_MS 30
#define Z2_AT_JSON_BODY_SAFE_MAX (64 * 1024)
#define PSRAM_STACK_ALIGN ARCH_STACK_PTR_ALIGN

#ifndef CLI_LOCAL_ECHO
#define CLI_LOCAL_ECHO 0
#endif

/* Kimi (Moonshot AI) fast path via ATKM AT command */
#define KIMI_HOST              "api.moonshot.cn"
#define KIMI_ENDPOINT          "https://api.moonshot.cn/v1/chat/completions"
#define KIMI_API_KEY           "sk-xxx"
#define KIMI_MODEL             "moonshot-v1-8k"
#define KIMI_TIMEOUT_MS        190000

static const struct device *const cli_uart = DEVICE_DT_GET(CLI_UART_NODE);
#if DT_NODE_HAS_STATUS(DT_ALIAS(uart3), okay)
#define HAVE_Z2_UART 1
static const struct device *const z2_uart = DEVICE_DT_GET(DT_ALIAS(uart3));
#else
#define HAVE_Z2_UART 0
static const struct device *const z2_uart = NULL;
#endif

static struct k_mutex claw_lock;
static struct k_mutex z2_uart_lock;
static uint8_t *claw_out;
static uintptr_t rust_heap_base;
static size_t rust_heap_size;
static bool display_ready;
static uint16_t *display_fb;
static uint16_t display_width;
static uint16_t display_height;

/* ── GNU Unifont PSRAM state ─────────────────────────────────────────────
 * The unifont binary is loaded from SD into a reserved PSRAM slice at boot.
 * Binary layout (generated by tools/gen_unifont_bin.py):
 *   [0..3]   Magic "UNIF"
 *   [4..7]   Version 1  (big-endian uint32)
 *   [8..11]  BMP entry count = 65536 (big-endian uint32)
 *   [12..]   65536 × 33 bytes:
 *              byte 0     : glyph width (0=absent, 8=halfwidth, 16=fullwidth)
 *              bytes 1-32 : 16 rows × uint16_t big-endian,
 *                           bit 15 = leftmost column (MSB-first)
 */
#define UNIFONT_MAGIC        0x554E4946UL  /* "UNIF" */
#define UNIFONT_VERSION      1U
#define UNIFONT_BMP_SIZE     65536U
#define UNIFONT_ENTRY_SIZE   33U           /* 1 + 16×2 */
#define UNIFONT_HEADER_SIZE  12U           /* magic(4)+ver(4)+count(4) */
#define UNIFONT_PSRAM_MAX    (UNIFONT_HEADER_SIZE + UNIFONT_BMP_SIZE * UNIFONT_ENTRY_SIZE)
/* = 12 + 65536×33 = 2 162 700 bytes ≈ 2.06 MB */
#define UNIFONT_BIN_PATH     RUSTMCUCLAW_DATA_DIR "/unifont.bin"

static uint8_t *unifont_psram;   /* PSRAM slice reserved in init_psram_layout */
static bool     unifont_loaded;  /* set to true after successful SD load */

/* ── Chat display state ───────────────────────────────────────────────────
 * A ring buffer of text lines rendered onto the LCD after each CLI
 * round-trip.  All access is from the CLI thread only; no locking needed.
 *
 * Screen: 410 × 502 px, corner radius 80 px.
 *   Title bar : y = 0 … DISPLAY_TITLE_H-1   (80 px, rounded corners)
 *   Separator : y = DISPLAY_TITLE_H-6 … DISPLAY_TITLE_H-5
 *   Chat area : y = DISPLAY_TITLE_H + DISPLAY_TITLE_GAP … bottom safe edge
 */
#define DISPLAY_MAX_LINES    70   /* ring-buffer history depth              */
#define DISPLAY_LINE_MAX     160  /* max bytes/line; fits ~50 CJK (3 B each) */
#define DISPLAY_LINE_HEIGHT  18   /* 16-px glyph (GNU Unifont) + 2-px gap   */
#define DISPLAY_MARGIN_X      8   /* left/right padding                     */
#define DISPLAY_TITLE_H      80   /* title bar height = corner radius       */
#define DISPLAY_TITLE_GAP     4   /* gap between separator and first line   */
#define DISPLAY_CORNER_PAD   84   /* bottom safe margin (radius + gap)      */
#define DISPLAY_COL_TITLE    0x07FFU  /* cyan       – app title/user/AI role */
#define DISPLAY_COL_DEVICE   0x8410U  /* mid-gray   – device subtitle         */
#define DISPLAY_COL_SEP      0x4208U  /* dark gray  – separator               */
#define DISPLAY_COL_USER     0x07FFU  /* cyan       – user input              */
#define DISPLAY_COL_REPLY    0xFFFFU  /* white      – main reply body         */
#define DISPLAY_COL_SYSTEM   0x9EFBU  /* pale cyan  – system/tool markers     */
#define DISPLAY_COL_LOADING  0x8410U  /* gray       – loading/thinking        */

#define TOUCH_STACK_SIZE     2048
#define TOUCH_THREAD_PRIORITY 7
#define SCROLL_SWIPE_MIN_PX  20  /* minimum vertical swipe distance to trigger scroll */

/* disp_lines / disp_colors live in PSRAM (allocated in init_psram_layout)
 * so they don't consume scarce SRAM.  NULL until init_psram_layout runs. */
static char (*disp_lines)[DISPLAY_LINE_MAX + 1];      /* PSRAM ptr */
static uint16_t *disp_colors;                          /* PSRAM ptr */
static int      disp_head;    /* ring: index of oldest entry */
static int      disp_count;   /* 0 .. DISPLAY_MAX_LINES      */
static volatile int disp_scroll_offset; /* lines scrolled up from newest; 0 = bottom */

/* ── Loading animation ──────────────────────────────────────────────────
 * display_lock serialises all writes to display_fb + LCD transfers so the
 * animation work item (system workqueue) and the CLI thread never race. */
static K_MUTEX_DEFINE(display_lock);
static volatile bool    llm_loading;
static volatile uint8_t anim_frame;

static void anim_timer_cb(struct k_timer *t);
static void anim_work_handler(struct k_work *w);
static K_TIMER_DEFINE(anim_timer, anim_timer_cb, NULL);
static K_WORK_DEFINE(anim_work, anim_work_handler);

/* ── Touch device ────────────────────────────────────────────────────── */
#if HAVE_TOUCH
static const struct device *const touch_dev =
    DEVICE_DT_GET(DT_NODELABEL(touch_device));
#else
static const struct device *const touch_dev = NULL;
#endif

static k_thread_stack_t cli_stack_area[CLI_STACK_SIZE]
__aligned(PSRAM_STACK_ALIGN);
static k_thread_stack_t poll_stack_area[POLL_STACK_SIZE]
__aligned(PSRAM_STACK_ALIGN);
static k_thread_stack_t touch_stack_area[TOUCH_STACK_SIZE]
__aligned(PSRAM_STACK_ALIGN);
static struct k_thread touch_thread_data;
#if HAVE_Z2_UART
/* Z2 URC reader stack lives in PSRAM (allocated in init_psram_layout) to
 * avoid eating scarce SRAM. */
static k_thread_stack_t *z2_urc_stack_area;
static struct k_thread z2_urc_thread_data;
/* FSEV reassembly scratch (PSRAM slot, see init_psram_layout). */
static uint8_t *fsev_buf;
static bool     z2_urc_enabled;
#endif

static void log_thread_stack_usage(struct k_thread *thread, const char *name)
{
#if defined(CONFIG_THREAD_STACK_INFO)
    size_t unused = 0;
    int ret;

    if (!thread || !name)
    {
        return;
    }

    ret = k_thread_stack_space_get(thread, &unused);
    if (ret == 0)
    {
        LOG_INF("%s stack unused=%u", name, (unsigned)unused);
    }
    else
    {
        LOG_WRN("%s stack query failed: %d", name, ret);
    }
#else
    ARG_UNUSED(thread);
    ARG_UNUSED(name);
#endif
}

K_MSGQ_DEFINE(cli_uart_rx_msgq, sizeof(uint8_t), CLI_UART_RX_QUEUE_LEN, 4);
static volatile uint32_t cli_uart_rx_drops;

#if HAVE_Z2_UART
K_MSGQ_DEFINE(z2_uart_rx_msgq, sizeof(uint8_t), Z2_UART_RX_QUEUE_LEN, 4);
static volatile uint32_t z2_uart_rx_drops;
#endif

static void kick_watchdog(void);

static int init_psram_layout(void);
static int init_display(void);
static int cli_uart_irq_init(void);

static FATFS fat_fs;
static struct fs_mount_t data_mount =
{
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = RUSTMCUCLAW_FS_MOUNT_POINT,
    .flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

/*
 * Some standalone RTL87X3G link sets reference HAL timer helpers that
 * are ROM-patched in full watch builds.  Keep weak fallbacks here so this app
 * links independently; platform builds that provide the real symbols override
 * these definitions.
 */
__weak bool TIM_GetTimerID(void *timx, uint32_t *p_ret)
{
    ARG_UNUSED(timx);
    if (p_ret)
    {
        *p_ret = 0;
    }
    return false;
}

__weak bool TIM_GetTimerShareBase(void *timx, void **p_ret)
{
    ARG_UNUSED(timx);
    if (p_ret)
    {
        *p_ret = NULL;
    }
    return false;
}

void __aeabi_unwind_cpp_pr0(void) {}
void __aeabi_unwind_cpp_pr1(void) {}
void __aeabi_unwind_cpp_pr2(void) {}

static void serial_write(const char *s)
{
    if (!s)
    {
        return;
    }
    while (*s)
    {
        if (*s == '\n')
        {
            uart_poll_out(cli_uart, '\r');
        }
        uart_poll_out(cli_uart, *s++);
    }
}

static void uart_write_buf(const struct device *uart, const uint8_t *buf, size_t len)
{
    if (!uart || !buf)
    {
        return;
    }

    for (size_t i = 0; i < len; ++i)
    {
        uart_poll_out(uart, buf[i]);
    }
}

static void uart_write_str(const struct device *uart, const char *s)
{
    if (!s)
    {
        return;
    }
    uart_write_buf(uart, (const uint8_t *)s, strlen(s));
}

static void uart_flush_input(const struct device *uart)
{
    unsigned char c;

    if (!uart)
    {
        return;
    }

#if HAVE_Z2_UART
    if (uart == z2_uart)
    {
        k_msgq_purge(&z2_uart_rx_msgq);
        z2_uart_rx_drops = 0;
    }
#endif

    while (uart_poll_in(uart, &c) == 0)
    {
    }
}

#if HAVE_Z2_UART
static void z2_uart_irq_cb(const struct device *dev, void *user_data)
{
    uint8_t buf[32];

    ARG_UNUSED(user_data);

    if (uart_irq_update(dev) <= 0)
    {
        return;
    }

    while (uart_irq_rx_ready(dev))
    {
        int got = uart_fifo_read(dev, buf, sizeof(buf));

        if (got <= 0)
        {
            break;
        }

        for (int i = 0; i < got; ++i)
        {
            if (k_msgq_put(&z2_uart_rx_msgq, &buf[i], K_NO_WAIT) != 0)
            {
                uint8_t dropped;

                z2_uart_rx_drops++;
                (void)k_msgq_get(&z2_uart_rx_msgq, &dropped, K_NO_WAIT);
                (void)k_msgq_put(&z2_uart_rx_msgq, &buf[i], K_NO_WAIT);
            }
        }
    }
}

static int z2_uart_irq_init(void)
{
    int ret;

    if (!device_is_ready(z2_uart))
    {
        return -ENODEV;
    }

    k_msgq_purge(&z2_uart_rx_msgq);
    z2_uart_rx_drops = 0;
    ret = uart_irq_callback_user_data_set(z2_uart, z2_uart_irq_cb, NULL);
    if (ret < 0)
    {
        return ret;
    }

    uart_irq_rx_enable(z2_uart);
    return 0;
}
#endif

static void cli_uart_irq_cb(const struct device *dev, void *user_data)
{
    uint8_t buf[32];

    ARG_UNUSED(user_data);

    if (uart_irq_update(dev) <= 0)
    {
        return;
    }

    while (uart_irq_rx_ready(dev))
    {
        int got = uart_fifo_read(dev, buf, sizeof(buf));

        if (got <= 0)
        {
            break;
        }

        for (int i = 0; i < got; ++i)
        {
            if (k_msgq_put(&cli_uart_rx_msgq, &buf[i], K_NO_WAIT) != 0)
            {
                uint8_t dropped;

                cli_uart_rx_drops++;
                (void)k_msgq_get(&cli_uart_rx_msgq, &dropped, K_NO_WAIT);
                (void)k_msgq_put(&cli_uart_rx_msgq, &buf[i], K_NO_WAIT);
            }
        }
    }
}

static int cli_uart_irq_init(void)
{
    int ret;

    if (!device_is_ready(cli_uart))
    {
        return -ENODEV;
    }

    k_msgq_purge(&cli_uart_rx_msgq);
    cli_uart_rx_drops = 0;
    ret = uart_irq_callback_user_data_set(cli_uart, cli_uart_irq_cb, NULL);
    if (ret < 0)
    {
        return ret;
    }

    uart_irq_rx_enable(cli_uart);
    return 0;
}

static int uart_read_byte(const struct device *uart, unsigned char *out, int timeout_ms)
{
    int64_t deadline = k_uptime_get() + timeout_ms;

    if (!uart || !out)
    {
        return -EINVAL;
    }

    while (k_uptime_get() < deadline)
    {
        int remaining_ms = (int)(deadline - k_uptime_get());

        if (remaining_ms <= 0)
        {
            break;
        }

#if HAVE_Z2_UART
        if (uart == z2_uart)
        {
            int wait_ms = remaining_ms > 20 ? 20 : remaining_ms;

            if (k_msgq_get(&z2_uart_rx_msgq, out, K_MSEC(wait_ms)) == 0)
            {
                kick_watchdog();
                return 0;
            }

            kick_watchdog();
            continue;
        }
#endif

        if (uart_poll_in(uart, out) == 0)
        {
            kick_watchdog();
            return 0;
        }

        k_sleep(K_MSEC(2));
        kick_watchdog();
    }

    return -EAGAIN;
}

static int uart_read_line(const struct device *uart, char *out, size_t out_cap, int timeout_ms)
{
    int64_t deadline = k_uptime_get() + timeout_ms;
    size_t len = 0;

    if (!uart || !out || out_cap < 2)
    {
        return -EINVAL;
    }

    out[0] = '\0';

    while (k_uptime_get() < deadline)
    {
        unsigned char c;
        int byte_ret;

        byte_ret = uart_read_byte(uart, &c, 20);
        if (byte_ret == -EAGAIN)
        {
            continue;
        }
        if (byte_ret < 0)
        {
            return byte_ret;
        }

        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            if (len == 0)
            {
                continue;
            }
            out[len] = '\0';
            return (int)len;
        }

        if (len + 1 >= out_cap)
        {
            out[len] = '\0';
            return -ENOBUFS;
        }

        out[len++] = (char)c;
    }

    if (len > 0)
    {
        out[len] = '\0';
        return (int)len;
    }

    return -EAGAIN;
}

static bool contains_forbidden_at_char(const char *s)
{
    if (!s)
    {
        return false;
    }

    while (*s)
    {
        if (*s == ',' || *s == '\r' || *s == '\n')
        {
            return true;
        }
        ++s;
    }

    return false;
}

static bool contains_forbidden_at_bytes(const uint8_t *buf, size_t len)
{
    if (!buf)
    {
        return false;
    }

    for (size_t i = 0; i < len; ++i)
    {
        if (buf[i] == '\0' || buf[i] == '\r' || buf[i] == '\n')
        {
            return true;
        }
    }

    return false;
}

static bool has_http_result_tokens(const char *line)
{
    if (!line)
    {
        return false;
    }

    return strstr(line, "ret=") != NULL && strstr(line, "http=") != NULL;
}

static bool looks_like_json_payload(const char *line)
{
    const char *cursor = line;

    if (!cursor)
    {
        return false;
    }

    while (*cursor == ' ' || *cursor == '\t')
    {
        ++cursor;
    }

    if (*cursor != '{')
    {
        return false;
    }

    return strstr(cursor, "\"choices\"") != NULL ||
           strstr(cursor, "\"error\"") != NULL ||
           strstr(cursor, "\"id\"") != NULL;
}

static bool json_scan_char(char c,
                           bool *started,
                           bool *in_string,
                           bool *escaped,
                           int *depth)
{
    if (!started || !in_string || !escaped || !depth)
    {
        return false;
    }

    if (!*started)
    {
        if (c == '{')
        {
            *started = true;
            *depth = 1;
        }
        return false;
    }

    if (*escaped)
    {
        *escaped = false;
        return false;
    }

    if (*in_string)
    {
        if (c == '\\')
        {
            *escaped = true;
        }
        else if (c == '"')
        {
            *in_string = false;
        }
        return false;
    }

    if (c == '"')
    {
        *in_string = true;
        return false;
    }

    if (c == '{')
    {
        (*depth)++;
        return false;
    }

    if (c == '}')
    {
        (*depth)--;
        return *depth == 0;
    }

    return false;
}

static int64_t uart_read_json_body(const struct device *uart,
                                   const char *initial,
                                   uint8_t *out,
                                   size_t out_cap,
                                   int64_t deadline)
{
    size_t len = 0;
    bool started = false;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;

    if (!uart || !out || out_cap < 2)
    {
        return -EINVAL;
    }

    out[0] = '\0';

    if (initial)
    {
        const char *json = strchr(initial, '{');

        if (json)
        {
            size_t frag_len = strlen(json);

            if (frag_len + 1 > out_cap)
            {
                return -ENOBUFS;
            }

            memcpy(out, json, frag_len);
            len = frag_len;
            out[len] = '\0';

            for (size_t i = 0; i < len; ++i)
            {
                if (json_scan_char((char)out[i],
                                   &started,
                                   &in_string,
                                   &escaped,
                                   &depth))
                {
                    return (int64_t)(i + 1);
                }
            }
        }
    }

    while (k_uptime_get() < deadline)
    {
        unsigned char c;
        int byte_ret;

        byte_ret = uart_read_byte(uart, &c, 20);
        if (byte_ret == -EAGAIN)
        {
            continue;
        }
        if (byte_ret < 0)
        {
            return byte_ret;
        }

        if (!started)
        {
            if (c != '{')
            {
                continue;
            }
            started = true;
        }

        if (len + 1 >= out_cap)
        {
            out[len] = '\0';
            return -ENOBUFS;
        }

        out[len++] = (char)c;
        out[len] = '\0';

        if (json_scan_char((char)c, &started, &in_string, &escaped, &depth))
        {
            return (int64_t)len;
        }
    }

    return started ? -EPROTO : -ETIMEDOUT;
}

static int parse_https_endpoint(const char *endpoint,
                                char *host,
                                size_t host_cap,
                                uint16_t *port,
                                char *path,
                                size_t path_cap)
{
    const char *cursor = endpoint;
    const char *host_start;
    const char *host_end;
    const char *colon;
    const char *slash;
    size_t host_len;
    size_t path_len;

    if (!endpoint || !host || !port || !path || host_cap < 2 || path_cap < 2)
    {
        return -EINVAL;
    }

    if (strncmp(cursor, "https://", 8) == 0)
    {
        cursor += 8;
        *port = 443;
    }
    else if (strncmp(cursor, "http://", 7) == 0)
    {
        return -EPROTONOSUPPORT;
    }
    else
    {
        *port = 443;
    }

    host_start = cursor;
    slash = strchr(cursor, '/');
    host_end = slash ? slash : (cursor + strlen(cursor));
    colon = memchr(host_start, ':', (size_t)(host_end - host_start));

    if (colon)
    {
        char port_buf[8];
        size_t port_len = (size_t)(host_end - colon - 1);
        long parsed_port;

        host_end = colon;
        if (port_len == 0 || port_len >= sizeof(port_buf))
        {
            return -EINVAL;
        }
        memcpy(port_buf, colon + 1, port_len);
        port_buf[port_len] = '\0';
        parsed_port = strtol(port_buf, NULL, 10);
        if (parsed_port <= 0 || parsed_port > 65535)
        {
            return -EINVAL;
        }
        *port = (uint16_t)parsed_port;
    }

    host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len + 1 > host_cap)
    {
        return -ENAMETOOLONG;
    }
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    if (contains_forbidden_at_char(host))
    {
        return -EINVAL;
    }

    if (slash)
    {
        path_len = strlen(slash);
        if (path_len + 1 > path_cap)
        {
            return -ENAMETOOLONG;
        }
        memcpy(path, slash, path_len + 1);
    }
    else
    {
        strcpy(path, "/");
    }

    if (contains_forbidden_at_char(path))
    {
        return -EINVAL;
    }

    return 0;
}

static int parse_atkm_result(const char *line, int *remote_ret, int *http_status)
{
    const char *ret_ptr;
    const char *http_ptr;
    char *endp;
    long value;

    if (!line || !remote_ret || !http_status)
    {
        return -EINVAL;
    }

    ret_ptr = strstr(line, "ret=");
    http_ptr = strstr(line, "http=");
    if (!ret_ptr || !http_ptr)
    {
        return -EINVAL;
    }

    value = strtol(ret_ptr + 4, &endp, 10);
    if (endp == ret_ptr + 4)
    {
        return -EINVAL;
    }
    *remote_ret = (int)value;

    value = strtol(http_ptr + 5, &endp, 10);
    if (endp == http_ptr + 5)
    {
        return -EINVAL;
    }
    *http_status = (int)value;

    return 0;
}

static int parse_athp_result(const char *line, int *remote_ret, int *http_status)
{
    const char *ret_ptr;
    const char *http_ptr;
    char *endp;
    long value;

    if (!line || !remote_ret || !http_status)
    {
        return -EINVAL;
    }

    ret_ptr = strstr(line, "ret=");
    http_ptr = strstr(line, "http=");
    if (!ret_ptr || !http_ptr)
    {
        return -EINVAL;
    }

    value = strtol(ret_ptr + 4, &endp, 10);
    if (endp == ret_ptr + 4)
    {
        return -EINVAL;
    }
    *remote_ret = (int)value;

    value = strtol(http_ptr + 5, &endp, 10);
    if (endp == http_ptr + 5)
    {
        return -EINVAL;
    }
    *http_status = (int)value;

    return 0;
}

static void z2plus_log_line(const char *tag, const char *line)
{
    char preview[161];
    size_t len;

    if (!tag || !line)
    {
        return;
    }

    len = strlen(line);
    if (len >= sizeof(preview))
    {
        memcpy(preview, line, sizeof(preview) - 4);
        preview[sizeof(preview) - 4] = '.';
        preview[sizeof(preview) - 3] = '.';
        preview[sizeof(preview) - 2] = '.';
        preview[sizeof(preview) - 1] = '\0';
    }
    else
    {
        memcpy(preview, line, len + 1);
    }

    LOG_INF("Z2Plus %s: %s", tag, preview);
}

static int z2plus_wait_line_contains(const char *needle,
                                     char *line,
                                     size_t line_cap,
                                     int64_t deadline)
{
    while (k_uptime_get() < deadline)
    {
        int remaining_ms = (int)(deadline - k_uptime_get());
        int line_len;

        if (remaining_ms <= 0)
        {
            break;
        }

        line_len = uart_read_line(z2_uart,
                                  line,
                                  line_cap,
                                  remaining_ms > 1000 ? 1000 : remaining_ms);
        if (line_len == -EAGAIN)
        {
            continue;
        }
        if (line_len < 0)
        {
            return line_len;
        }
        if (line_len == 0)
        {
            continue;
        }

        z2plus_log_line("RX", line);

        if (strstr(line, "ERROR") != NULL)
        {
            LOG_ERR("Z2Plus RX error while waiting for '%s'", needle ? needle : "<any>");
            return -EIO;
        }
        if (needle && strstr(line, needle) != NULL)
        {
            return 0;
        }
    }

    return -ETIMEDOUT;
}

static void z2plus_abort_stream_transfer(void)
{
    LOG_WRN("Z2Plus TX: ATHX");
    uart_write_str(z2_uart, "ATHX\r\n");
}

static void serial_write_buf(const uint8_t *buf)
{
    if (!buf || buf[0] == '\0')
    {
        return;
    }
    serial_write((const char *)buf);
}

static void disable_watchdog_and_dlps(void)
{
    int32_t pm_ret;

    if (wdg_is_enable())
    {
        WDG_Disable();
        LOG_INF("Watchdog disabled");
    }
    else
    {
        LOG_INF("Watchdog already disabled");
    }

    bt_power_mode_set(BTPOWER_ACTIVE);
    pm_ret = power_mode_set(POWER_ACTIVE_MODE);
    LOG_INF("power_mode_set(POWER_ACTIVE_MODE) ret=%d", pm_ret);

    pm_ret = power_mode_pause();
    LOG_INF("power_mode_pause() ret=%d", pm_ret);
}

static void kick_watchdog(void)
{
    if (wdg_is_enable())
    {
        WDG_Kick();
    }
}

static int init_psram_layout(void)
{
    uintptr_t base = (uintptr_t)RUSTMCUCLAW_PSRAM_BASE;
    uintptr_t limit = base + (uintptr_t)RUSTMCUCLAW_PSRAM_SIZE;
    uintptr_t cursor = base;

    cursor = ROUND_UP(cursor, sizeof(uintptr_t));
    claw_out = (uint8_t *)cursor;
    cursor += CLAW_OUT_MAX;

#if HAVE_Z2_UART
    /* Feishu URC reassembly scratch — header lines + payload + tail marker. */
    cursor = ROUND_UP(cursor, sizeof(uintptr_t));
    fsev_buf = (uint8_t *)cursor;
    cursor += FSEV_BUF_MAX;
    /* URC reader thread stack (kept in PSRAM to spare SRAM). */
    cursor = ROUND_UP(cursor, PSRAM_STACK_ALIGN);
    z2_urc_stack_area = (k_thread_stack_t *)cursor;
    cursor += Z2_URC_STACK_SIZE;
#endif

    /* Chat display ring buffer (moved from SRAM to PSRAM). */
    cursor = ROUND_UP(cursor, sizeof(uintptr_t));
    disp_lines  = (char (*)[DISPLAY_LINE_MAX + 1])cursor;
    cursor += (size_t)DISPLAY_MAX_LINES * (DISPLAY_LINE_MAX + 1);
    cursor = ROUND_UP(cursor, sizeof(uint16_t));
    disp_colors = (uint16_t *)cursor;
    cursor += (size_t)DISPLAY_MAX_LINES * sizeof(uint16_t);
    /* Zero-initialise so the ring buffer starts clean. */
    memset(disp_lines,  0, (size_t)DISPLAY_MAX_LINES * (DISPLAY_LINE_MAX + 1));
    memset(disp_colors, 0, (size_t)DISPLAY_MAX_LINES * sizeof(uint16_t));

    /* Reserve PSRAM for the full GNU Unifont BMP binary (~2.06 MB). */
    cursor = ROUND_UP(cursor, sizeof(uintptr_t));
    if (cursor + UNIFONT_PSRAM_MAX > limit)
    {
        LOG_ERR("PSRAM too small for unifont: need %u available %u",
                (unsigned)UNIFONT_PSRAM_MAX,
                (unsigned)(limit - cursor));
        return -ENOMEM;
    }
    unifont_psram = (uint8_t *)cursor;
    cursor += UNIFONT_PSRAM_MAX;

    cursor = ROUND_UP(cursor, sizeof(uintptr_t));
    if (cursor >= limit)
    {
        return -ENOMEM;
    }

    rust_heap_base = cursor;
    rust_heap_size = limit - cursor;

    LOG_INF("PSRAM carve: claw_out=%p(%u) unifont=%p(%u) rust_heap=%p(%u)",
            (void *)claw_out,
            (unsigned)CLAW_OUT_MAX,
            (void *)unifont_psram,
            (unsigned)UNIFONT_PSRAM_MAX,
            (void *)(uintptr_t)rust_heap_base,
            (unsigned)rust_heap_size);

    return 0;
}

/* ── GNU Unifont PSRAM loader ────────────────────────────────────────────
 * Reads unifont.bin from SD card directly into the reserved PSRAM slice.
 * Must be called after mount_data_fs().  Sets unifont_loaded on success.
 */
static int unifont_load(void)
{
    int64_t got;
    uint32_t magic, version, count;

    if (!unifont_psram)
    {
        LOG_ERR("Unifont PSRAM slice not allocated");
        return -ENOMEM;
    }

    LOG_INF("Loading unifont from %s (%u bytes) ...", UNIFONT_BIN_PATH,
            (unsigned)UNIFONT_PSRAM_MAX);
    kick_watchdog();
    got = claw_mcu_fs_read(UNIFONT_BIN_PATH, unifont_psram, UNIFONT_PSRAM_MAX);
    kick_watchdog();
    if (got < 0)
    {
        LOG_ERR("Unifont read failed: %lld (file missing?)", (long long)got);
        return (int)got;
    }
    if ((size_t)got < UNIFONT_HEADER_SIZE)
    {
        LOG_ERR("Unifont file too short: %lld bytes", (long long)got);
        return -EINVAL;
    }

    /* Validate header (big-endian). */
    magic   = ((uint32_t)unifont_psram[0] << 24) | ((uint32_t)unifont_psram[1] << 16)
              | ((uint32_t)unifont_psram[2] <<  8) | (uint32_t)unifont_psram[3];
    version = ((uint32_t)unifont_psram[4] << 24) | ((uint32_t)unifont_psram[5] << 16)
              | ((uint32_t)unifont_psram[6] <<  8) | (uint32_t)unifont_psram[7];
    count   = ((uint32_t)unifont_psram[8] << 24) | ((uint32_t)unifont_psram[9] << 16)
              | ((uint32_t)unifont_psram[10] <<  8) | (uint32_t)unifont_psram[11];

    if (magic != UNIFONT_MAGIC)
    {
        LOG_ERR("Unifont bad magic: 0x%08X (expected 0x%08X)",
                (unsigned)magic, (unsigned)UNIFONT_MAGIC);
        return -EINVAL;
    }
    if (version != UNIFONT_VERSION)
    {
        LOG_ERR("Unifont unsupported version: %u", (unsigned)version);
        return -EINVAL;
    }
    if (count != UNIFONT_BMP_SIZE)
    {
        LOG_ERR("Unifont unexpected BMP size: %u", (unsigned)count);
        return -EINVAL;
    }

    unifont_loaded = true;
    LOG_INF("Unifont loaded: %lld bytes, BMP glyphs ready", (long long)got);
    return 0;
}

/* Look up a Unicode codepoint in the PSRAM-resident GNU Unifont bitmap.
 * Called from Rust via FFI.  Returns glyph width (8 or 16) on success,
 * or 0 if absent / unifont not yet loaded. */
int claw_mcu_unifont_get_glyph(uint32_t codepoint, uint16_t *out_rows)
{
    const uint8_t *entry;
    uint8_t width;
    int i;

    if (!unifont_loaded || !unifont_psram || !out_rows
        || codepoint >= UNIFONT_BMP_SIZE)
    {
        return 0;
    }

    entry = unifont_psram + UNIFONT_HEADER_SIZE
            + (size_t)codepoint * UNIFONT_ENTRY_SIZE;
    width = entry[0];
    if (width == 0)
    {
        return 0;
    }

    for (i = 0; i < 16; ++i)
    {
        out_rows[i] = ((uint16_t)entry[1 + i * 2] << 8)
                      | (uint16_t)entry[1 + i * 2 + 1];
    }
    return (int)width;
}

static int display_blit_rgb565(const uint16_t *pixels, uint16_t width, uint16_t height)
{
    size_t pixels_count;
    size_t bytes;
    uint16_t x0;
    uint16_t y0;

    if (!display_ready || !display_fb || !pixels)
    {
        return -ENODEV;
    }

    if (width == 0U || height == 0U || width > display_width || height > display_height)
    {
        return -EINVAL;
    }

    pixels_count = (size_t)width * (size_t)height;
    bytes = pixels_count * sizeof(uint16_t);
    if (bytes > (size_t)RUSTMCUCLAW_FB_BYTES)
    {
        return -ENOMEM;
    }

    memcpy(display_fb, pixels, bytes);
    (void)sys_cache_data_flush_range(display_fb, bytes);

    x0 = (display_width > width) ? (display_width - width) / 2U : 0U;
    y0 = (display_height > height) ? (display_height - height) / 2U : 0U;

    kick_watchdog();
    rtk_lcd_hal_transfer_done();
    rtk_lcd_hal_set_window(x0, y0, width, height);
    rtk_lcd_hal_start_transfer((uint8_t *)display_fb, pixels_count);
    rtk_lcd_hal_transfer_done();
    kick_watchdog();

    return 0;
}

int claw_mcu_display_width(void)
{
    return display_ready ? (int)display_width : -ENODEV;
}

int claw_mcu_display_height(void)
{
    return display_ready ? (int)display_height : -ENODEV;
}

int claw_mcu_display_fill_rgb565(uint16_t color)
{
    size_t pixels_count;
    size_t i;

    if (!display_ready || !display_fb)
    {
        return -ENODEV;
    }

    pixels_count = (size_t)display_width * (size_t)display_height;
    if ((pixels_count * sizeof(uint16_t)) > (size_t)RUSTMCUCLAW_FB_BYTES)
    {
        return -ENOMEM;
    }

    for (i = 0; i < pixels_count; ++i)
    {
        display_fb[i] = color;
    }

    return display_blit_rgb565(display_fb, display_width, display_height);
}

int claw_mcu_display_present_rgb565(const uint16_t *pixels, uint16_t width, uint16_t height)
{
    return display_blit_rgb565(pixels, width, height);
}

static int init_display(void)
{
    size_t fb_bytes;
    uintptr_t fb_region_base = (uintptr_t)RUSTMCUCLAW_FB_REGION_BASE;
    size_t fb_region_size = (size_t)RUSTMCUCLAW_FB_REGION_SIZE;

    display_fb = (uint16_t *)(uintptr_t)RUSTMCUCLAW_FB_BASE;

    if (fb_region_size < ((size_t)RUSTMCUCLAW_FB_BYTES + (size_t)RUSTMCUCLAW_FB_GUARD_BYTES))
    {
        LOG_ERR("LCD fb region too small: region=%u need=%u+guard=%u",
                (unsigned)fb_region_size,
                (unsigned)RUSTMCUCLAW_FB_BYTES,
                (unsigned)RUSTMCUCLAW_FB_GUARD_BYTES);
        display_fb = NULL;
        return -ENOMEM;
    }

    LOG_INF("LCD init: region=%p(%u) fb=%p(%u) guard=%u",
            (void *)fb_region_base,
            (unsigned)fb_region_size,
            display_fb,
            (unsigned)RUSTMCUCLAW_FB_BYTES,
            (unsigned)RUSTMCUCLAW_FB_GUARD_BYTES);
    rtk_lcd_hal_init();

    display_width = (uint16_t)rtk_lcd_hal_get_width();
    display_height = (uint16_t)rtk_lcd_hal_get_height();
    fb_bytes = (size_t)display_width * (size_t)display_height * sizeof(uint16_t);
    if (fb_bytes > (size_t)RUSTMCUCLAW_FB_BYTES)
    {
        LOG_ERR("LCD framebuffer too small: need=%u have=%u",
                (unsigned)fb_bytes,
                (unsigned)RUSTMCUCLAW_FB_BYTES);
        display_fb = NULL;
        return -ENOMEM;
    }

    display_ready = true;
    LOG_INF("LCD ready: %ux%u %u-bit",
            (unsigned)display_width,
            (unsigned)display_height,
            (unsigned)rtk_lcd_hal_get_pixel_bits());

    return claw_mcu_display_fill_rgb565(0xff00);
}

static bool to_fatfs_path(const char *path, char *out, size_t out_size)
{
    const char *src = path;
    size_t len;

    if (!path || !out || out_size == 0)
    {
        return false;
    }

    if (src[0] == '/')
    {
        src++;
    }

    len = strlen(src);
    if (len + 1 > out_size)
    {
        return false;
    }

    memcpy(out, src, len + 1);
    return true;
}

static bool path_exists(const char *path)
{
    FILINFO info;
    char fatfs_path[256];
    FRESULT fr;

    if (!path)
    {
        return false;
    }

    if (!to_fatfs_path(path, fatfs_path, sizeof(fatfs_path)))
    {
        return false;
    }

    kick_watchdog();
    memset(&info, 0, sizeof(info));
    fr = f_stat(fatfs_path, &info);
    kick_watchdog();

    if (fr == FR_OK)
    {
        return true;
    }

    if (fr == FR_NO_FILE || fr == FR_NO_PATH || fr == FR_INVALID_NAME)
    {
        return false;
    }

    LOG_WRN("FatFS stat failed: %d (%s)", fr, path);
    return false;
}

static void make_parent_dir(const char *path)
{
    char tmp[256];
    const char *slash;

    if (!path)
    {
        return;
    }

    slash = strrchr(path, '/');
    if (!slash || slash == path)
    {
        return;
    }

    size_t len = (size_t)(slash - path);
    if (len >= sizeof(tmp))
    {
        return;
    }

    memcpy(tmp, path, len);
    tmp[len] = '\0';

    if (!path_exists(tmp))
    {
        (void)fs_mkdir(tmp);
    }
}

static int mount_data_fs(void)
{
    kick_watchdog();
    int ret = fs_mount(&data_mount);
    kick_watchdog();
    if (ret == 0 || ret == -EALREADY)
    {
        kick_watchdog();
        ret = fs_mkdir(RUSTMCUCLAW_DATA_DIR);
        kick_watchdog();
        if (ret == 0 || ret == -EEXIST)
        {
            return 0;
        }

        LOG_ERR("PSRAM data dir create failed: %d (%s)", ret, RUSTMCUCLAW_DATA_DIR);
        return ret;
    }

    LOG_ERR("PSRAM FATFS mount failed: %d", ret);
    return ret;
}
int claw_mcu_read_temp_humidity(int32_t *temp_milli_c, int32_t *humidity_milli_pct)
{
    const struct device *dht = DEVICE_DT_GET(DT_NODELABEL(dht11_sensor));
    struct sensor_value temp;
    struct sensor_value humi;
    int ret;

    if (!temp_milli_c || !humidity_milli_pct)
    {
        return -EINVAL;
    }

    if (!device_is_ready(dht))
    {
        LOG_WRN("DHT11: device not ready (check P1_0 wiring)");
        return -ENODEV;
    }

    /* DHT11 needs a short stabilization period after power-up. */
    k_sleep(K_MSEC(1000));

    kick_watchdog();
    ret = sensor_sample_fetch(dht);
    kick_watchdog();
    if (ret < 0)
    {
        LOG_WRN("DHT11: sample_fetch failed: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(dht, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    if (ret < 0)
    {
        LOG_WRN("DHT11: ambient temp read failed: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(dht, SENSOR_CHAN_HUMIDITY, &humi);
    if (ret < 0)
    {
        LOG_WRN("DHT11: humidity read failed: %d", ret);
        return ret;
    }

    *temp_milli_c = (int32_t)sensor_value_to_milli(&temp);
    *humidity_milli_pct = (int32_t)sensor_value_to_milli(&humi);
    LOG_INF("DHT11 OK: T=%d.%03d C RH=%d.%03d %%",
            *temp_milli_c / 1000,
            abs(*temp_milli_c % 1000),
            *humidity_milli_pct / 1000,
            abs(*humidity_milli_pct % 1000));
    return 0;
}

int64_t claw_mcu_fs_read(const char *path, uint8_t *out, size_t out_cap)
{
    struct fs_file_t file;
    ssize_t got;

    if (!path || !out || out_cap == 0)
    {
        return -EINVAL;
    }

    if (!path_exists(path))
    {
        return -ENOENT;
    }

    fs_file_t_init(&file);
    kick_watchdog();
    int ret = fs_open(&file, path, FS_O_READ);
    if (ret < 0)
    {
        return ret;
    }

    kick_watchdog();
    got = fs_read(&file, out, out_cap);
    (void)fs_close(&file);
    kick_watchdog();
    if (got < 0)
    {
        return got;
    }
    return got;
}

int claw_mcu_fs_exists(const char *path)
{
    if (!path)
    {
        return -EINVAL;
    }

    return path_exists(path) ? 1 : 0;
}

/* List entries of a directory.
 *
 * Writes a newline-separated list of `name<TAB>type<TAB>size` records into
 * `out` (NUL-terminated). `type` is either "file" or "dir"; `size` is the
 * regular-file size in bytes, or 0 for directories.
 *
 * Returns the number of bytes written (not counting the trailing NUL) on
 * success, or a negative Zephyr error on failure. Truncates silently if the
 * output buffer is too small.
 */
int64_t claw_mcu_fs_list(const char *path, uint8_t *out, size_t out_cap)
{
    struct fs_dir_t dir;
    int ret;
    size_t pos = 0;

    if (!path || !out || out_cap == 0)
    {
        return -EINVAL;
    }

    /* Reserve 1 byte for the terminating NUL. */
    size_t writable = out_cap - 1;
    out[0] = '\0';

    fs_dir_t_init(&dir);
    kick_watchdog();
    ret = fs_opendir(&dir, path);
    if (ret < 0)
    {
        return ret;
    }

    for (;;)
    {
        struct fs_dirent entry;
        kick_watchdog();
        ret = fs_readdir(&dir, &entry);
        if (ret < 0)
        {
            (void)fs_closedir(&dir);
            return ret;
        }
        if (entry.name[0] == '\0')
        {
            break; /* end of directory */
        }

        const char *type =
            (entry.type == FS_DIR_ENTRY_DIR) ? "dir" : "file";
        char line[320];
        int n = snprintf(line, sizeof(line), "%s\t%s\t%u\n",
                         entry.name, type, (unsigned)entry.size);
        if (n <= 0)
        {
            continue;
        }
        size_t len = (size_t)n;
        if (pos + len > writable)
        {
            break; /* truncate silently */
        }
        memcpy(out + pos, line, len);
        pos += len;
    }
    (void)fs_closedir(&dir);
    out[pos] = '\0';
    return (int64_t)pos;
}

int claw_mcu_fs_write(const char *path, const uint8_t *data, size_t data_len, bool append)
{
    struct fs_file_t file;
    int flags = FS_O_CREATE | FS_O_WRITE;

    if (!path || (!data && data_len != 0))
    {
        return -EINVAL;
    }

    make_parent_dir(path);
    if (append)
    {
        flags |= FS_O_APPEND;
    }

    fs_file_t_init(&file);
    kick_watchdog();
    int ret = fs_open(&file, path, flags);
    if (ret < 0)
    {
        return ret;
    }

    if (!append)
    {
        kick_watchdog();
        ret = fs_truncate(&file, 0);
        if (ret < 0)
        {
            (void)fs_close(&file);
            return ret;
        }
    }

    kick_watchdog();
    ssize_t written = fs_write(&file, data, data_len);
    (void)fs_close(&file);
    kick_watchdog();
    if (written < 0)
    {
        return (int)written;
    }
    return written == (ssize_t)data_len ? 0 : -EIO;
}

static int seed_boot_config(void)
{
    static const char boot_toml[] = RUSTMCUCLAW_BOOT_TOML;

    /* Only seed if the config file does not already exist, so that user
     * edits on the SD card survive reboots. */
    struct fs_dirent entry;
    if (fs_stat(RUSTMCUCLAW_BOOT_CFG_PATH, &entry) == 0)
    {
        LOG_INF("Boot rmcc.toml already exists, skipping seed: %s",
                RUSTMCUCLAW_BOOT_CFG_PATH);
        return 0;
    }

    int ret = claw_mcu_fs_write(RUSTMCUCLAW_BOOT_CFG_PATH,
                                (const uint8_t *)boot_toml,
                                sizeof(boot_toml) - 1,
                                false);
    if (ret < 0)
    {
        LOG_ERR("Boot rmcc.toml write failed: %d (%s)", ret, RUSTMCUCLAW_BOOT_CFG_PATH);
        return ret;
    }

    LOG_INF("Boot rmcc.toml seeded: %s", RUSTMCUCLAW_BOOT_CFG_PATH);
    return 0;
}

int64_t z2plus_https_post_json(const char *endpoint,
                               const char *api_key,
                               const uint8_t *body,
                               size_t body_len,
                               uint8_t *out,
                               size_t out_cap)
{
#if !HAVE_Z2_UART
    ARG_UNUSED(endpoint);
    ARG_UNUSED(api_key);
    ARG_UNUSED(body);
    ARG_UNUSED(body_len);
    ARG_UNUSED(out);
    ARG_UNUSED(out_cap);
    return -ENOTSUP;
#else
    char host[Z2_HOST_MAX];
    char path[Z2_PATH_MAX];
    char status_line[Z2_STATUS_LINE_MAX];
    char port_buf[8];
    const char *token;
    uint16_t port;
    int ret;
    int remote_ret = -ETIMEDOUT;
    int http_status = -1;
    bool saw_result = false;
    int64_t deadline;

    if (!device_is_ready(z2_uart))
    {
        return -ENODEV;
    }
    if (!endpoint || !body || body_len == 0 || !out || out_cap < 2)
    {
        return -EINVAL;
    }
    if (body_len > Z2_AT_JSON_BODY_SAFE_MAX)
    {
        LOG_ERR("Z2Plus body too large for chunked transfer: %u > %u",
                (unsigned)body_len, (unsigned)Z2_AT_JSON_BODY_SAFE_MAX);
        return -EMSGSIZE;
    }
    if (contains_forbidden_at_bytes(body, body_len))
    {
        return -EINVAL;
    }

    ret = parse_https_endpoint(endpoint, host, sizeof(host), &port, path, sizeof(path));
    if (ret < 0)
    {
        return ret;
    }

    token = (api_key && api_key[0] != '\0') ? api_key : "-";
    if (contains_forbidden_at_char(token))
    {
        return -EINVAL;
    }

    ret = snprintf(port_buf, sizeof(port_buf), "%u", (unsigned int)port);
    if (ret <= 0 || ret >= (int)sizeof(port_buf))
    {
        return -EINVAL;
    }

    out[0] = '\0';

    k_mutex_lock(&z2_uart_lock, K_FOREVER);
    uart_flush_input(z2_uart);
    LOG_INF("Z2Plus TX: ATHB len=%u host=%s port=%u path=%s token=%s",
            (unsigned)body_len,
            host,
            (unsigned)port,
            path,
            (token[0] == '-' && token[1] == '\0') ? "-" : "<set>");
    uart_write_str(z2_uart, "ATHB=");
    snprintf(status_line, sizeof(status_line), "%u,", (unsigned int)body_len);
    uart_write_str(z2_uart, status_line);
    uart_write_str(z2_uart, host);
    uart_write_str(z2_uart, ",");
    uart_write_str(z2_uart, port_buf);
    uart_write_str(z2_uart, ",");
    uart_write_str(z2_uart, path);
    uart_write_str(z2_uart, ",");
    uart_write_str(z2_uart, token);
    uart_write_str(z2_uart, "\r\n");

    deadline = k_uptime_get() + Z2_HTTPS_TIMEOUT_MS;
    ret = z2plus_wait_line_contains("[ATHB] OK", status_line, sizeof(status_line), deadline);
    if (ret < 0)
    {
        k_mutex_unlock(&z2_uart_lock);
        return ret;
    }

    for (size_t offset = 0; offset < body_len; offset += Z2_AT_CHUNK_SIZE)
    {
        size_t chunk_len = body_len - offset;

        if (chunk_len > Z2_AT_CHUNK_SIZE)
        {
            chunk_len = Z2_AT_CHUNK_SIZE;
        }

        LOG_INF("Z2Plus TX: ATHD offset=%u len=%u",
                (unsigned)offset,
                (unsigned)chunk_len);

        ret = snprintf(status_line, sizeof(status_line), "ATHD=%u:", (unsigned int)chunk_len);
        if (ret <= 0 || ret >= (int)sizeof(status_line))
        {
            z2plus_abort_stream_transfer();
            k_mutex_unlock(&z2_uart_lock);
            return -EINVAL;
        }

        uart_write_str(z2_uart, status_line);
        uart_write_buf(z2_uart, body + offset, chunk_len);

        ret = z2plus_wait_line_contains("[ATHD] OK", status_line, sizeof(status_line), deadline);
        if (ret < 0)
        {
            z2plus_abort_stream_transfer();
            k_mutex_unlock(&z2_uart_lock);
            return ret;
        }

        k_msleep(Z2_AT_CHUNK_THROTTLE_MS);
    }

    LOG_INF("Z2Plus TX: ATHE");
    uart_write_str(z2_uart, "ATHE\r\n");

    while (k_uptime_get() < deadline)
    {
        int remaining_ms = (int)(deadline - k_uptime_get());
        int line_len;

        if (remaining_ms <= 0)
        {
            break;
        }

        line_len = uart_read_line(z2_uart,
                                  status_line,
                                  sizeof(status_line),
                                  remaining_ms > 1000 ? 1000 : remaining_ms);
        if (line_len == -EAGAIN)
        {
            continue;
        }
        if (line_len < 0)
        {
            remote_ret = line_len;
            break;
        }
        if (line_len == 0)
        {
            continue;
        }

        if (!saw_result)
        {
            if (has_http_result_tokens(status_line))
            {
                if (parse_athp_result(status_line, &remote_ret, &http_status) < 0)
                {
                    remote_ret = -EPROTO;
                    break;
                }
                saw_result = true;
                if (remote_ret != 0)
                {
                    LOG_ERR("Z2Plus ATHP failed: ret=%d http=%d", remote_ret, http_status);
                    break;
                }

                LOG_INF("Z2Plus ATHP result ok: http=%d, waiting full JSON body", http_status);
                remote_ret = (int)uart_read_json_body(z2_uart, NULL, out, out_cap, deadline);
                if (remote_ret < 0)
                {
                    LOG_ERR("Z2Plus ATHP body read failed: %d", remote_ret);
                    break;
                }

#if HAVE_Z2_UART
                if (z2_uart_rx_drops != 0)
                {
                    LOG_WRN("Z2Plus UART RX dropped %u byte(s)", (unsigned)z2_uart_rx_drops);
                }
#endif

                if (http_status < 200 || http_status >= 300)
                {
                    LOG_ERR("Z2Plus ATHP HTTP %d body: %s", http_status, (const char *)out);
                }

                LOG_INF("Z2Plus ATHP ok: http=%d bytes=%u", http_status, (unsigned)remote_ret);
                k_mutex_unlock(&z2_uart_lock);
                return remote_ret;
            }
            else if (strchr(status_line, '{') != NULL || looks_like_json_payload(status_line))
            {
                LOG_WRN("Z2Plus ATHP body arrived without explicit result line");
                remote_ret = (int)uart_read_json_body(z2_uart, status_line, out, out_cap, deadline);
                if (remote_ret >= 0)
                {
                    k_mutex_unlock(&z2_uart_lock);
                    return remote_ret;
                }
                break;
            }
            continue;
        }

        continue;
    }

    k_mutex_unlock(&z2_uart_lock);
    return remote_ret;
#endif
}

/*
 * z2plus_kimi_chat - send a plain-text prompt to Kimi via ATKM AT command.
 * Uses the AmebaZ2+ ATKM shortcut (api key / model hardcoded on Z2 side).
 * Returns number of bytes written to `out` on success, negative on error.
 */
static int64_t z2plus_kimi_chat(const char *prompt, uint8_t *out, size_t out_cap)
{
#if !HAVE_Z2_UART
    ARG_UNUSED(prompt);
    ARG_UNUSED(out);
    ARG_UNUSED(out_cap);
    return -ENOTSUP;
#else
    char status_line[Z2_STATUS_LINE_MAX];
    int remote_ret = -ETIMEDOUT;
    int http_status = -1;
    bool saw_result = false;
    int64_t deadline;

    if (!device_is_ready(z2_uart))
    {
        return -ENODEV;
    }
    if (!prompt || !prompt[0] || !out || out_cap < 2)
    {
        return -EINVAL;
    }

    out[0] = '\0';

    k_mutex_lock(&z2_uart_lock, K_FOREVER);
    uart_flush_input(z2_uart);
    uart_write_str(z2_uart, "ATKM=");
    uart_write_str(z2_uart, prompt);
    uart_write_str(z2_uart, "\r\n");

    deadline = k_uptime_get() + KIMI_TIMEOUT_MS;
    while (k_uptime_get() < deadline)
    {
        int remaining_ms = (int)(deadline - k_uptime_get());
        int line_len;

        if (remaining_ms <= 0)
        {
            break;
        }

        line_len = uart_read_line(z2_uart, status_line, sizeof(status_line),
                                  remaining_ms > 1000 ? 1000 : remaining_ms);
        if (line_len == -EAGAIN)
        {
            continue;
        }
        if (line_len < 0)
        {
            remote_ret = line_len;
            break;
        }
        if (line_len == 0)
        {
            continue;
        }

        if (!saw_result)
        {
            if (has_http_result_tokens(status_line))
            {
                if (parse_atkm_result(status_line, &remote_ret, &http_status) < 0)
                {
                    remote_ret = -EPROTO;
                    break;
                }
                saw_result = true;
                if (remote_ret != 0)
                {
                    LOG_ERR("Kimi ATKM failed: ret=%d http=%d", remote_ret, http_status);
                    break;
                }

                LOG_INF("Kimi ATKM result ok: http=%d, waiting full JSON body", http_status);
                remote_ret = (int)uart_read_json_body(z2_uart, NULL, out, out_cap, deadline);
                if (remote_ret < 0)
                {
                    LOG_ERR("Kimi ATKM body read failed: %d", remote_ret);
                    break;
                }

#if HAVE_Z2_UART
                if (z2_uart_rx_drops != 0)
                {
                    LOG_WRN("Z2Plus UART RX dropped %u byte(s)", (unsigned)z2_uart_rx_drops);
                }
#endif

                if (http_status < 200 || http_status >= 300)
                {
                    LOG_ERR("Kimi ATKM HTTP %d body: %s", http_status, (const char *)out);
                }

                LOG_INF("Kimi ATKM ok: http=%d bytes=%u", http_status, (unsigned)remote_ret);
                k_mutex_unlock(&z2_uart_lock);
                return remote_ret;
            }
            else if (strchr(status_line, '{') != NULL || looks_like_json_payload(status_line))
            {
                LOG_WRN("Kimi ATKM body arrived without explicit result line");
                remote_ret = (int)uart_read_json_body(z2_uart, status_line, out, out_cap, deadline);
                if (remote_ret >= 0)
                {
                    k_mutex_unlock(&z2_uart_lock);
                    return remote_ret;
                }
                break;
            }
            continue;
        }

        continue;
    }

    k_mutex_unlock(&z2_uart_lock);
    return remote_ret;
#endif
}

int64_t claw_mcu_https_post_json(const char *endpoint,
                                 const char *api_key,
                                 const uint8_t *body,
                                 size_t body_len,
                                 uint8_t *out,
                                 size_t out_cap)
{
    return z2plus_https_post_json(endpoint, api_key, body, body_len, out, out_cap);
}

uint64_t claw_mcu_now_secs(void)
{
    if (wall_clock_synced)
    {
        int64_t delta_ms = k_uptime_get() - wall_clock_base_uptime_ms;
        if (delta_ms < 0)
        {
            delta_ms = 0;
        }
        return wall_clock_base_secs + (uint64_t)(delta_ms / 1000);
    }
    return RUSTMCUCLAW_UNIX_EPOCH_OFFSET + (uint64_t)(k_uptime_get() / 1000);
}

/*
 * Parse a single `[ATEP] ret=<n> epoch=<n>` status line from the Z2Plus AT
 * firmware. Returns 0 if both fields were recognised.
 */
static int parse_atep_result(const char *line, int *remote_ret, uint64_t *epoch_out)
{
    const char *ret_ptr;
    const char *epoch_ptr;
    char *endp;
    long long val;

    if (!line || !remote_ret || !epoch_out)
    {
        return -EINVAL;
    }

    ret_ptr = strstr(line, "ret=");
    epoch_ptr = strstr(line, "epoch=");
    if (!ret_ptr || !epoch_ptr)
    {
        return -EINVAL;
    }

    val = strtoll(ret_ptr + 4, &endp, 10);
    if (endp == ret_ptr + 4)
    {
        return -EINVAL;
    }
    *remote_ret = (int)val;

    val = strtoll(epoch_ptr + 6, &endp, 10);
    if (endp == epoch_ptr + 6)
    {
        return -EINVAL;
    }
    *epoch_out = (uint64_t)(val > 0 ? val : 0);

    return 0;
}

/*
 * z2plus_sync_internet_time - issue ATEP over the Z2Plus AT UART, parse the
 * resulting Unix epoch, and update the global wall-clock baseline so that
 * `claw_mcu_now_secs()` returns real UTC seconds.
 *
 * Returns 0 on success and stores the synced epoch in *epoch_out when given,
 * negative errno otherwise.
 */
static int z2plus_sync_internet_time_host(const char *host, uint64_t *epoch_out)
{
#if !HAVE_Z2_UART
    ARG_UNUSED(host);
    ARG_UNUSED(epoch_out);
    return -ENOTSUP;
#else
    char status_line[Z2_STATUS_LINE_MAX];
    char cmd[96];
    int64_t deadline;
    int remote_ret = -ETIMEDOUT;
    uint64_t epoch = 0;

    if (!device_is_ready(z2_uart))
    {
        return -ENODEV;
    }
    if (host == NULL || host[0] == '\0')
    {
        host = ATEP_DEFAULT_HOST;
    }

    snprintf(cmd, sizeof(cmd), "ATEP=%s,%d\r\n", host, ATEP_SNTP_TIMEOUT_S);

    k_mutex_lock(&z2_uart_lock, K_FOREVER);
    uart_flush_input(z2_uart);
    uart_write_str(z2_uart, cmd);

    deadline = k_uptime_get() + ATEP_TIMEOUT_MS;
    while (k_uptime_get() < deadline)
    {
        int remaining_ms = (int)(deadline - k_uptime_get());
        int line_len;

        if (remaining_ms <= 0)
        {
            break;
        }

        line_len = uart_read_line(z2_uart, status_line, sizeof(status_line),
                                  remaining_ms > 500 ? 500 : remaining_ms);
        if (line_len == -EAGAIN)
        {
            continue;
        }
        if (line_len < 0)
        {
            remote_ret = line_len;
            break;
        }
        if (line_len == 0)
        {
            continue;
        }

        /* Filter for lines that carry our marker to avoid stray AT chatter. */
        if (strstr(status_line, "[ATEP]") == NULL)
        {
            continue;
        }
        if (strstr(status_line, "epoch=") == NULL)
        {
            /* The first informational "[ATEP]: SNTP epoch sync" line. */
            continue;
        }

        if (parse_atep_result(status_line, &remote_ret, &epoch) < 0)
        {
            remote_ret = -EPROTO;
            break;
        }
        break;
    }

    k_mutex_unlock(&z2_uart_lock);

    if (remote_ret == 0 && epoch > 0)
    {
        int64_t now_ms = k_uptime_get();
        wall_clock_base_secs = epoch;
        wall_clock_base_uptime_ms = now_ms;
        wall_clock_synced = true;
        if (epoch_out)
        {
            *epoch_out = epoch;
        }
        LOG_INF("Wall-clock synced via Z2Plus SNTP (%s): epoch=%llu", host, (unsigned long long)epoch);
        return 0;
    }

    LOG_WRN("Z2Plus SNTP sync failed (%s): ret=%d", host, remote_ret);
    return remote_ret == 0 ? -EPROTO : remote_ret;
#endif
}

/*
 * z2plus_sync_internet_time - try the default host, then sequentially fall
 * back to other servers in `k_atep_hosts[]`. Returns 0 as soon as any one
 * succeeds.
 */
static int z2plus_sync_internet_time(uint64_t *epoch_out)
{
    int ret = -ETIMEDOUT;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(k_atep_hosts); i++)
    {
        kick_watchdog();
        ret = z2plus_sync_internet_time_host(k_atep_hosts[i], epoch_out);
        if (ret == 0)
        {
            return 0;
        }
        /* -ENODEV / -ENOTSUP are not host-specific; stop early. */
        if (ret == -ENODEV || ret == -ENOTSUP)
        {
            return ret;
        }
    }
    return ret;
}

/*
 * "time" / "time sync" - print the current epoch or re-sync from the network.
 *   time         -> show wall-clock state and current epoch.
 *   time sync    -> trigger an SNTP sync via Z2Plus ATEP.
 */
static bool try_time_cmd(char *line)
{
    if (strcmp(line, "time") != 0 && strcmp(line, "time sync") != 0)
    {
        return false;
    }

    if (strcmp(line, "time sync") == 0)
    {
        uint64_t epoch = 0;
        int ret;

        serial_write("[time] Syncing via Z2Plus SNTP...\r\n");
        ret = z2plus_sync_internet_time(&epoch);
        if (ret == 0)
        {
            char buf[80];
            snprintf(buf, sizeof(buf),
                     "[time] Synced. epoch=%llu\r\n",
                     (unsigned long long)epoch);
            serial_write(buf);
        }
        else
        {
            char buf[80];
            snprintf(buf, sizeof(buf),
                     "[time] Sync failed: %d\r\n", ret);
            serial_write(buf);
        }
        return true;
    }

    {
        uint64_t now = claw_mcu_now_secs();
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "[time] synced=%d epoch=%llu uptime_ms=%lld\r\n",
                 (int)wall_clock_synced,
                 (unsigned long long)now,
                 (long long)k_uptime_get());
        serial_write(buf);
    }
    return true;
}

/*
 * Built-in "kimi <prompt>" CLI command.
 * Bypasses the Rust library and sends the prompt directly to AmebaZ2+ ATKM.
 * Usage (serial terminal):  kimi 今天天气怎么样?
 */
static bool try_kimi_cmd(char *line)
{
    const char *prefix = "kimi ";
    const size_t prefix_len = 5; /* strlen("kimi ") */
    const char *prompt;
    int64_t ret;

    if (strncmp(line, prefix, prefix_len) != 0)
    {
        return false;
    }

    prompt = line + prefix_len;
    while (*prompt == ' ')
    {
        prompt++;
    }
    if (!prompt[0])
    {
        serial_write("Usage: kimi <prompt>\r\n");
        return true;
    }

    serial_write("[kimi] Sending to Kimi...\r\n");
    ret = z2plus_kimi_chat(prompt, claw_out, CLAW_OUT_MAX);
    if (ret < 0)
    {
        char err_buf[64];
        snprintf(err_buf, sizeof(err_buf), "[kimi] Error: %lld\r\n", (long long)ret);
        serial_write(err_buf);
    }
    else
    {
        serial_write("[kimi] Response:\r\n");
        serial_write_buf(claw_out);
        serial_write("\r\n");
    }
    return true;
}

/*
 * "cfg" - print current provider configuration baked into firmware.
 * "help" - print all built-in CLI commands.
 */
static bool try_builtin_cmd(char *line)
{
    if (strcmp(line, "cfg") == 0)
    {
        serial_write("\r\n=== Claw Provider Config ===\r\n");
        serial_write("kind     : " RUSTMCUCLAW_BOOT_PROVIDER_KIND "\r\n");
        serial_write("model    : " RUSTMCUCLAW_BOOT_PROVIDER_MODEL "\r\n");
        serial_write("endpoint : " RUSTMCUCLAW_BOOT_PROVIDER_ENDPOINT "\r\n");
        serial_write("cfg_path : " RUSTMCUCLAW_BOOT_CFG_PATH "\r\n");
        serial_write("============================\r\n");
        return true;
    }

    if (strcmp(line, "help") == 0)
    {
        serial_write("\r\n=== Built-in Commands ===\r\n");
        serial_write("  kimi <prompt>  Send prompt directly to Kimi via ATKM (fast path)\r\n");
        serial_write("  time           Show wall-clock state / current Unix epoch\r\n");
        serial_write("  time sync      Sync wall-clock from Internet via Z2Plus SNTP (ATEP)\r\n");
        serial_write("  cfg            Show provider configuration\r\n");
        serial_write("  help           Show this help\r\n");
        serial_write("  <any other>    Forwarded to RustMcuClaw engine\r\n");
        serial_write("========================\r\n");
        return true;
    }

    return false;
}

/* ── Chat display helpers ───────────────────────────────────────────────── */

/* Append one line to the ring buffer.  When full, the oldest entry is
 * overwritten.  Safe to call before rustmcuclaw_mcu_init (no heap use). */
static void display_chat_push(const char *line, uint16_t color)
{
    int slot;

    if (!display_ready || !display_fb || !disp_lines || !disp_colors)
    {
        return;
    }

    if (disp_count < DISPLAY_MAX_LINES)
    {
        slot = disp_count++;
    }
    else
    {
        /* Ring full: overwrite the oldest entry. */
        slot = disp_head;
        disp_head = (disp_head + 1) % DISPLAY_MAX_LINES;
    }

    strncpy(disp_lines[slot], line, DISPLAY_LINE_MAX);
    disp_lines[slot][DISPLAY_LINE_MAX] = '\0';
    disp_colors[slot] = color;
}

static uint16_t display_classify_line_color(const char *line, uint16_t default_color)
{
    if (!line || !line[0])
    {
        return default_color;
    }

    if (default_color != DISPLAY_COL_REPLY)
    {
        return default_color;
    }

    if (strncmp(line, "--- assistant", 13) == 0)
    {
        return DISPLAY_COL_USER;
    }

    if (strncmp(line, "--- tool results", 16) == 0 ||
        strncmp(line, "[tool ", 6) == 0 ||
        strncmp(line, "[note]", 6) == 0 ||
        strncmp(line, "[LLM error]", 11) == 0 ||
        strncmp(line, "[Memory ", 8) == 0 ||
        strncmp(line, "OK:", 3) == 0 ||
        strncmp(line, "ERR:", 4) == 0)
    {
        return DISPLAY_COL_SYSTEM;
    }

    return default_color;
}

/* Split multi-line text (e.g. claw_out) on \n, hard-wrap at pixel boundary,
 * and push each non-empty, non-prompt line.
 *
 * Pixel cost per byte:
 *   ASCII  (0x00-0x7F)  → 8 px halfwidth
 *   UTF-8 lead (≥0xC0)  → 16 px fullwidth (covers all CJK)
 *   UTF-8 cont (0x80-0xBF) → 0 px (not a glyph start)
 *
 * This guarantees we never split inside a multi-byte UTF-8 sequence,
 * so Rust's from_utf8() never sees invalid bytes (which would produce
 * a silent blank line). */
static void display_chat_add_text(const char *text, uint16_t color)
{
    char   buf[DISPLAY_LINE_MAX + 1];
    size_t buf_len = 0;
    int    pixel_w = 0;
    /* Usable width: leave DISPLAY_MARGIN_X on both sides. */
    const int max_px = 410 - 2 * DISPLAY_MARGIN_X; /* 394 px */
    const char *p;

    if (!text)
    {
        return;
    }

    for (p = text; *p; ++p)
    {
        unsigned char c = (unsigned char) * p;
        int char_px;

        if (c == '\r')
        {
            continue;
        }
        if (c == '\n')
        {
            buf[buf_len] = '\0';
            if (buf_len > 0 &&
                strcmp(buf, "claw>") != 0 &&
                strncmp(buf, "claw> ", 6) != 0)
            {
                display_chat_push(buf, display_classify_line_color(buf, color));
            }
            buf_len = 0;
            pixel_w = 0;
            continue;
        }

        if (c < 0x80U)
        {
            char_px = 8;   /* ASCII halfwidth */
        }
        else if (c >= 0xC0U)
        {
            char_px = 16;  /* UTF-8 lead byte → fullwidth */
        }
        else
        {
            char_px = 0;   /* UTF-8 continuation byte, no new glyph */
        }

        /* Wrap BEFORE adding a glyph that would overflow the line. */
        if (char_px > 0 && pixel_w + char_px > max_px && buf_len > 0)
        {
            buf[buf_len] = '\0';
            if (strcmp(buf, "claw>") != 0 &&
                strncmp(buf, "claw> ", 6) != 0)
            {
                display_chat_push(buf, display_classify_line_color(buf, color));
            }
            buf_len = 0;
            pixel_w = 0;
        }

        if (buf_len < (size_t)DISPLAY_LINE_MAX)
        {
            buf[buf_len++] = (char)c;
            pixel_w += char_px;
        }
    }

    if (buf_len > 0)
    {
        buf[buf_len] = '\0';
        if (strcmp(buf, "claw>") != 0 &&
            strncmp(buf, "claw> ", 6) != 0)
        {
            display_chat_push(buf, display_classify_line_color(buf, color));
        }
    }
}

static const char *display_skip_heartbeat_prefix(const char *text)
{
    const char *cursor = text;

    if (!cursor)
    {
        return NULL;
    }

    while (strncmp(cursor, "[heartbeat]", 11) == 0)
    {
        const char *newline = strchr(cursor, '\n');
        if (!newline)
        {
            return "";
        }
        cursor = newline + 1;

        while (*cursor == '\r' || *cursor == '\n')
        {
            cursor++;
        }
        while (strncmp(cursor, "claw>", 5) == 0)
        {
            const char *prompt_newline = strchr(cursor, '\n');
            if (!prompt_newline)
            {
                while (*cursor == ' ' || *cursor == '\r' || *cursor == '\n')
                {
                    cursor++;
                }
                break;
            }
            cursor = prompt_newline + 1;
            while (*cursor == '\r' || *cursor == '\n')
            {
                cursor++;
            }
        }
    }

    return cursor;
}

/* Draw the fixed header: app title, device name, time, separator line.
 * Called by display_chat_render(); safe to call before mcu_init. */
static void display_draw_header(void)
{
    int title_w, title_x, x, sy;
    char time_buf[16];

    if (!display_fb || display_width == 0 || display_height == 0)
    {
        return;
    }

    /* ── Row 1: "Claw" title centred, scale 2 (16 px tall), y=14 ── */
    title_w = 7 * 8 * 2; /* "   Claw" = 7 chars */
    title_x = ((int)display_width - title_w) / 2;
    if (title_x < DISPLAY_MARGIN_X)
    {
        title_x = DISPLAY_MARGIN_X;
    }
    rustmcuclaw_mcu_draw_text_rgb565(
        display_fb, display_width, display_height,
        title_x, 14,
        "   Claw",
        DISPLAY_COL_TITLE, 2);

    /* ── Row 2: "RTL87X3G" left-aligned + HH:MM:SS right-aligned, y=44 ── */
    /* Left: device name */
    rustmcuclaw_mcu_draw_text_rgb565(
        display_fb, display_width, display_height,
        DISPLAY_MARGIN_X, 44,
        "RTL87X3G",
        DISPLAY_COL_DEVICE, 1);

    /* Right: current time (UTC+8).  Falls back to "--:--:--" before sync. */
    if (wall_clock_synced)
    {
        uint64_t now   = claw_mcu_now_secs();
        uint64_t cst   = now + 8ULL * 3600ULL; /* UTC+8 */
        uint32_t tod   = (uint32_t)(cst % 86400ULL);
        uint32_t hh    = tod / 3600u;
        uint32_t mm    = (tod % 3600u) / 60u;
        uint32_t ss    = tod % 60u;
        snprintf(time_buf, sizeof(time_buf), "%02u:%02u:%02u", hh, mm, ss);
    }
    else
    {
        snprintf(time_buf, sizeof(time_buf), "--:--:--");
    }
    {
        /* Right-align: 8 chars × 8 px × scale 1 = 64 px */
        int time_w = (int)strlen(time_buf) * 8;
        int time_x = (int)display_width - DISPLAY_MARGIN_X - time_w;
        if (time_x < DISPLAY_MARGIN_X)
        {
            time_x = DISPLAY_MARGIN_X;
        }
        rustmcuclaw_mcu_draw_text_rgb565(
            display_fb, display_width, display_height,
            time_x, 44,
            time_buf,
            DISPLAY_COL_TITLE, 1);
    }

    /* ── 2-pixel separator line just above the chat area ── */
    for (sy = DISPLAY_TITLE_H - 6; sy < DISPLAY_TITLE_H - 4; sy++)
    {
        for (x = DISPLAY_MARGIN_X;
             x < (int)display_width - DISPLAY_MARGIN_X; x++)
        {
            if (sy >= 0 && sy < (int)display_height)
            {
                display_fb[(size_t)sy * display_width + (size_t)x] =
                    DISPLAY_COL_SEP;
            }
        }
    }
}

/* Clear the framebuffer, draw the title bar and all buffered chat lines,
 * then push the result to the LCD.
 * Acquires display_lock internally so it is safe to call from any thread.
 * No-op when the display is not yet initialised. */
static void display_chat_render(void)
{
    int    i, y, idx, max_vis, start, end, max_scroll;
    int    chat_y_start, chat_y_end;
    int    bx, by;
    size_t pi, px_count;

    if (!display_ready || !display_fb ||
        display_width == 0 || display_height == 0)
    {
        return;
    }

    k_mutex_lock(&display_lock, K_FOREVER);

    /* Clear to black. */
    px_count = (size_t)display_width * (size_t)display_height;
    for (pi = 0; pi < px_count; ++pi)
    {
        display_fb[pi] = 0x0000U;
    }

    /* Title bar (title text + device name + separator). */
    display_draw_header();

    /* Chat area: below title bar, above bottom rounded corner. */
    chat_y_start = DISPLAY_TITLE_H + DISPLAY_TITLE_GAP;        /* 84  */
    chat_y_end   = (int)display_height - (int)DISPLAY_CORNER_PAD; /* 418 */
    max_vis      = (chat_y_end - chat_y_start) / DISPLAY_LINE_HEIGHT; /* 33 */

    if (max_vis <= 0)
    {
        claw_mcu_display_present_rgb565(display_fb, display_width, display_height);
        k_mutex_unlock(&display_lock);
        return;
    }

    if (disp_count == 0)
    {
        /* No chat lines yet – just show the spinner if loading. */
        if (llm_loading)
        {
            static const char *const spin_frames[] = {"|", "/", "-", "\\"};
            char spin_buf[20];
            snprintf(spin_buf, sizeof(spin_buf), "Thinking... %s",
                     spin_frames[anim_frame & 3u]);
            rustmcuclaw_mcu_draw_text_rgb565(
                display_fb, display_width, display_height,
                DISPLAY_MARGIN_X, chat_y_start,
                spin_buf, DISPLAY_COL_LOADING, 1);
        }
        claw_mcu_display_present_rgb565(display_fb, display_width, display_height);
        k_mutex_unlock(&display_lock);
        return;
    }

    /* Clamp scroll offset to valid range. */
    max_scroll = (disp_count > max_vis) ? (disp_count - max_vis) : 0;
    if (disp_scroll_offset > max_scroll)
    {
        disp_scroll_offset = max_scroll;
    }
    if (disp_scroll_offset < 0)
    {
        disp_scroll_offset = 0;
    }

    /* start = index (0-based into ring) of the first line to display.
     * 0 = scroll at bottom (newest), max_scroll = fully scrolled up. */
    start = max_scroll - disp_scroll_offset;
    if (start < 0)
    {
        start = 0;
    }
    end = start + max_vis;
    if (end > disp_count)
    {
        end = disp_count;
    }

    /* Render visible lines top-to-bottom. */
    y = chat_y_start;
    for (i = start; i < end; ++i)
    {
        idx = (disp_head + i) % DISPLAY_MAX_LINES;
        rustmcuclaw_mcu_draw_text_rgb565(
            display_fb, display_width, display_height,
            DISPLAY_MARGIN_X, y,
            disp_lines[idx],
            disp_colors[idx],
            1);
        y += DISPLAY_LINE_HEIGHT;
    }

    /* Loading spinner: shown below the last chat line when at bottom. */
    if (llm_loading && disp_scroll_offset == 0 && y < chat_y_end)
    {
        static const char *const spin_frames[] = {"|", "/", "-", "\\"};
        char spin_buf[20];
        snprintf(spin_buf, sizeof(spin_buf), "Thinking... %s",
                 spin_frames[anim_frame & 3u]);
        rustmcuclaw_mcu_draw_text_rgb565(
            display_fb, display_width, display_height,
            DISPLAY_MARGIN_X, y,
            spin_buf, DISPLAY_COL_LOADING, 1);
    }

    /* Scroll position indicator: thin vertical bar on the right edge,
     * visible only when scrolled up from the bottom. */
    if (disp_scroll_offset > 0 && max_scroll > 0)
    {
        int bar_range = chat_y_end - chat_y_start - 16;
        int bar_top   = chat_y_start +
                        (bar_range * (max_scroll - disp_scroll_offset) / max_scroll);
        for (by = bar_top; by < bar_top + 16 && by < chat_y_end; by++)
        {
            for (bx = (int)display_width - 5; bx < (int)display_width - 2; bx++)
            {
                if (bx >= 0 && by >= 0)
                {
                    display_fb[(size_t)by * display_width + (size_t)bx] = 0x7BEFu;
                }
            }
        }
    }

    claw_mcu_display_present_rgb565(display_fb, display_width, display_height);
    k_mutex_unlock(&display_lock);
}

/* ── Loading animation callbacks ─────────────────────────────────────────
 * anim_timer fires every 400 ms while an LLM call is in-flight and posts a
 * work item to the system workqueue.  The work handler re-renders the display
 * with the next spinner frame.  display_chat_render() already serialises
 * access to the framebuffer via display_lock. */
static void anim_timer_cb(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_work_submit(&anim_work);
}

static void anim_work_handler(struct k_work *w)
{
    ARG_UNUSED(w);
    if (!llm_loading)
    {
        return;
    }
    anim_frame = (anim_frame + 1u) & 3u;
    display_chat_render();
}

/* ── Touch scroll thread ─────────────────────────────────────────────────
 * Polls the CHSC6417 touch controller every 50 ms.  Implements a
 * follow-the-finger (跟手) scroll: the display tracks the finger in
 * real-time while it is held down rather than waiting for lift-off.
 *
 * A sub-line pixel accumulator (pixel_accum) converts continuous pixel
 * deltas into whole-line scrolls, so slow drags still register correctly.
 *
 *   finger moves DOWN (y increases) → scroll up   → show older messages
 *   finger moves UP   (y decreases) → scroll down → show newer messages
 *
 * The scroll offset is reset to 0 whenever new messages are pushed. */
static void touch_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

#if HAVE_TOUCH
    bool     prev_press = false;
    uint16_t last_y     = 0;
    int      pixel_accum = 0; /* sub-line pixel accumulator */

    if (!device_is_ready(touch_dev))
    {
        LOG_WRN("Touch device not ready; scroll disabled");
        return;
    }
    LOG_INF("Touch scroll (follow-finger) enabled");

    while (true)
    {
        k_sleep(K_MSEC(20));

        TOUCH_DATA t = get_raw_touch_data(touch_dev);

        if (t.is_press && !prev_press)
        {
            /* Rising edge: finger just touched the screen. */
            last_y      = t.y;
            pixel_accum = 0;
            prev_press  = true;

        }
        else if (t.is_press && prev_press)
        {
            /* Finger still down: follow-the-finger real-time scroll. */
            int dy = (int)t.y - (int)last_y;

            if (dy != 0)
            {
                pixel_accum += dy;

                /* Convert accumulated pixels into whole lines. */
                if (pixel_accum >= (int)DISPLAY_LINE_HEIGHT ||
                    pixel_accum <= -(int)DISPLAY_LINE_HEIGHT)
                {
                    int lines      = pixel_accum / (int)DISPLAY_LINE_HEIGHT;
                    int chat_h     = (int)display_height - DISPLAY_CORNER_PAD
                                     - DISPLAY_TITLE_H - DISPLAY_TITLE_GAP;
                    int max_vis    = (display_height > 0)
                                     ? (chat_h / (int)DISPLAY_LINE_HEIGHT) : 33;
                    int max_scroll = (disp_count > max_vis)
                                     ? (disp_count - max_vis) : 0;

                    disp_scroll_offset += lines;
                    if (disp_scroll_offset < 0)
                    {
                        disp_scroll_offset = 0;
                    }
                    if (disp_scroll_offset > max_scroll)
                    {
                        disp_scroll_offset = max_scroll;
                    }

                    /* Keep the remainder for smooth sub-line tracking. */
                    pixel_accum -= lines * (int)DISPLAY_LINE_HEIGHT;

                    display_chat_render();
                }

                last_y = t.y;
            }

        }
        else if (!t.is_press && prev_press)
        {
            /* Falling edge: finger lifted – discard any residual pixel. */
            pixel_accum = 0;
            prev_press  = false;
        }
    }
#else
    ARG_UNUSED(touch_dev);
    LOG_INF("Touch support not compiled (no DT touch_device node)");
#endif /* HAVE_TOUCH */
}

static void process_line(char *line)
{
    bool rust_handled;
    char user_buf[DISPLAY_LINE_MAX + 3]; /* "> " + content + NUL */

    /* Show the user's input immediately so the screen reflects the
     * in-flight query before the potentially long LLM round-trip.
     * Reset scroll to the bottom so the new message is always visible. */
    disp_scroll_offset = 0;
    snprintf(user_buf, sizeof(user_buf), "> %s", line);
    display_chat_push(user_buf, DISPLAY_COL_USER);
    display_chat_render();

    /* Start loading animation before the (potentially long) LLM call.
     * Builtin commands complete in <<400 ms so the spinner never appears. */
    llm_loading = true;
    k_timer_start(&anim_timer, K_MSEC(400), K_MSEC(400));

    k_mutex_lock(&claw_lock, K_FOREVER);
    kick_watchdog();
    memset(claw_out, 0, CLAW_OUT_MAX);

    /* Proactively reinitialise the Rust runtime when the PSRAM bump heap is
     * more than 70 % consumed.  The bump allocator never frees memory, so
     * without this the allocator exhausts after ~20 long LLM exchanges and
     * the thread freezes silently.  rustmcuclaw_mcu_init() resets the heap
     * pointer to its start before making any new allocations, so this is safe
     * even when the allocator is 100 % full. */
    {
        size_t heap_used = rustmcuclaw_mcu_heap_used();
        size_t heap_total = rustmcuclaw_mcu_heap_size();
        if (heap_total > 0 && heap_used > heap_total * 7 / 10)
        {
            int summary_ret;

            LOG_WRN("Rust heap %zu/%zu bytes (>70%%), reinitialising", heap_used, heap_total);
            memset(claw_out, 0, CLAW_OUT_MAX);
            summary_ret = rustmcuclaw_mcu_persist_session_summary(claw_out, CLAW_OUT_MAX);
            if (summary_ret > 0 && claw_out[0] != '\0')
            {
                LOG_INF("Persisted session summary before heap reset: %s", claw_out);
            }
            else if (summary_ret < 0)
            {
                LOG_WRN("Failed to persist session summary before heap reset: %d", summary_ret);
            }

            display_chat_push("[Memory full, history cleared]", DISPLAY_COL_SYSTEM);
            display_chat_render();
            memset(claw_out, 0, CLAW_OUT_MAX);
            (void)rustmcuclaw_mcu_init((uint8_t *)rust_heap_base,
                                       rust_heap_size,
                                       RUSTMCUCLAW_DATA_DIR,
                                       claw_out,
                                       CLAW_OUT_MAX);
            memset(claw_out, 0, CLAW_OUT_MAX);
        }
    }

    rust_handled = !try_builtin_cmd(line) && !try_kimi_cmd(line) && !try_time_cmd(line);
    if (rust_handled)
    {
        (void)rustmcuclaw_mcu_handle_line(line, claw_out, CLAW_OUT_MAX);
        log_thread_stack_usage(k_current_get(), "claw_cli");
        serial_write_buf(claw_out);
        /* Stop spinner first so the final render shows the reply, not the spinner. */
        k_timer_stop(&anim_timer);
        llm_loading = false;
        /* Push reply lines and re-render. */
        display_chat_add_text((const char *)claw_out, DISPLAY_COL_REPLY);
        display_chat_render();
    }
    else
    {
        k_timer_stop(&anim_timer);
        llm_loading = false;
    }
    kick_watchdog();
    k_mutex_unlock(&claw_lock);

    if (cli_uart_rx_drops != 0)
    {
        LOG_WRN("CLI UART RX dropped %u byte(s)", (unsigned)cli_uart_rx_drops);
        cli_uart_rx_drops = 0;
    }
}

#if HAVE_Z2_UART
/* ===========================================================================
 * Feishu long-connection bootstrap and URC reader.
 *
 * The Z2Plus AT firmware owns the wss long-connection to Feishu and re-emits
 * each downstream event as a multi-line URC block:
 *
 *     \r\n[FSEV-BEGIN]\r\n
 *     msg_id=...\r\n
 *     type=event\r\n
 *     sum=...\r\n
 *     seq=...\r\n
 *     trace_id=...\r\n
 *     payload_len=N\r\n
 *     <N raw bytes of UTF-8 JSON>\r\n
 *     [FSEV-END]\r\n
 *
 * `z2_urc_thread` watches the Z2 UART when no AT command is in flight,
 * collects the block into `fsev_buf`, then hands it to Rust via
 * `rustmcuclaw_mcu_handle_feishu_event` which itself replies to the chat
 * over `claw_mcu_https_post_json` (ATHB).
 * ===========================================================================
 */

/* Send `ATFL=<app_id>,<app_secret>` and wait briefly for the `[ATFL] ret=`
 * line. Returns the parsed `ret` value (0 on success) or a negative errno. */
static int z2plus_start_feishu_channel(const char *app_id, const char *app_secret)
{
    char status_line[Z2_STATUS_LINE_MAX];
    int64_t deadline;
    int parsed_ret = -ETIMEDOUT;

    if (!device_is_ready(z2_uart))
    {
        return -ENODEV;
    }
    if (!app_id || !app_id[0] || !app_secret || !app_secret[0])
    {
        return -EINVAL;
    }
    if (contains_forbidden_at_char(app_id) || contains_forbidden_at_char(app_secret))
    {
        return -EINVAL;
    }

    k_mutex_lock(&z2_uart_lock, K_FOREVER);
    uart_flush_input(z2_uart);
    uart_write_str(z2_uart, "ATFL=");
    uart_write_str(z2_uart, app_id);
    uart_write_str(z2_uart, ",");
    uart_write_str(z2_uart, app_secret);
    uart_write_str(z2_uart, "\r\n");

    deadline = k_uptime_get() + 30000; /* TLS handshake + bootstrap HTTP */
    while (k_uptime_get() < deadline)
    {
        int remaining_ms = (int)(deadline - k_uptime_get());
        int line_len;

        if (remaining_ms <= 0)
        {
            break;
        }
        line_len = uart_read_line(z2_uart, status_line, sizeof(status_line),
                                  remaining_ms > 1000 ? 1000 : remaining_ms);
        if (line_len == -EAGAIN)
        {
            continue;
        }
        if (line_len < 0)
        {
            parsed_ret = line_len;
            break;
        }
        if (line_len == 0)
        {
            continue;
        }
        // if (strstr(status_line, "[FSWS-CONNECTING]") ||
        //     strstr(status_line, "[FSWS-CONNECTED]")) {
        //     parsed_ret = 0;
        //     break;
        // }
        if (strstr(status_line, "[ATFL]") && strstr(status_line, "ret="))
        {
            const char *r = strstr(status_line, "ret=");
            parsed_ret = (int)strtol(r + 4, NULL, 10);
            break;
        }
    }

    k_mutex_unlock(&z2_uart_lock);
    return parsed_ret;
}

/* Read raw bytes (no \r filtering) directly from the Z2 UART msgq. Used to
 * fetch the binary payload after `payload_len=`. Returns bytes read, or a
 * negative errno on timeout/error. */
static int z2plus_read_raw(uint8_t *out, size_t want, int64_t deadline)
{
    size_t got = 0;
    while (got < want && k_uptime_get() < deadline)
    {
        int remaining_ms = (int)(deadline - k_uptime_get());
        unsigned char c;
        int br;

        if (remaining_ms <= 0)
        {
            break;
        }
        br = uart_read_byte(z2_uart, &c, remaining_ms > 500 ? 500 : remaining_ms);
        if (br == -EAGAIN)
        {
            continue;
        }
        if (br < 0)
        {
            return br;
        }
        out[got++] = c;
    }
    return (int)got;
}

/* Try to collect one FSEV block. Returns total bytes written to `fsev_buf`
 * on success, 0 if no FSEV-BEGIN was observed within the slice, or a
 * negative errno on protocol failure. Must be called with `z2_uart_lock`
 * held. */
static int z2plus_collect_fsev_block(int64_t outer_deadline)
{
    char line[Z2_STATUS_LINE_MAX];
    size_t pos = 0;
    size_t payload_len = 0;
    bool got_payload_len = false;
    int line_len;

    /* Wait briefly for any URC line. Most of the time there is nothing. */
    line_len = uart_read_line(z2_uart, line, sizeof(line), 50);
    if (line_len < 0)
    {
        return 0;
    }
    if (line_len == 0 || strstr(line, "[FSEV-BEGIN]") == NULL)
    {
        return 0;
    }

    /* Re-pack the block into fsev_buf in a canonical form so Rust can locate
     * "payload_len=" trivially. */
    int n = snprintf((char *)fsev_buf, FSEV_BUF_MAX, "[FSEV-BEGIN]\n");
    if (n <= 0 || (size_t)n >= FSEV_BUF_MAX)
    {
        return -ENOBUFS;
    }
    pos = (size_t)n;

    /* Read header key=value lines until we see `payload_len=`. */
    int64_t hdr_deadline = k_uptime_get() + 2000;
    while (!got_payload_len && k_uptime_get() < hdr_deadline)
    {
        line_len = uart_read_line(z2_uart, line, sizeof(line), 500);
        if (line_len == -EAGAIN)
        {
            continue;
        }
        if (line_len < 0)
        {
            return line_len;
        }
        if (line_len == 0)
        {
            continue;
        }
        n = snprintf((char *)fsev_buf + pos, FSEV_BUF_MAX - pos, "%s\n", line);
        if (n <= 0 || (size_t)n >= FSEV_BUF_MAX - pos)
        {
            return -ENOBUFS;
        }
        pos += (size_t)n;
        if (strncmp(line, "payload_len=", 12) == 0)
        {
            payload_len = (size_t)strtoul(line + 12, NULL, 10);
            got_payload_len = true;
        }
    }
    if (!got_payload_len)
    {
        return -EPROTO;
    }

    /* Read exactly payload_len raw bytes (may include embedded \n). */
    if (payload_len > 0)
    {
        if (payload_len > FSEV_BUF_MAX - pos - 32)
        {
            return -EMSGSIZE;
        }
        int got = z2plus_read_raw(fsev_buf + pos, payload_len,
                                  k_uptime_get() + 3000);
        if (got < 0)
        {
            return got;
        }
        if ((size_t)got != payload_len)
        {
            return -EAGAIN;
        }
        pos += payload_len;
    }

    /* Drain until [FSEV-END]. Tolerant of small trailing junk. */
    int64_t end_deadline = k_uptime_get() + 1500;
    while (k_uptime_get() < end_deadline)
    {
        line_len = uart_read_line(z2_uart, line, sizeof(line), 300);
        if (line_len == -EAGAIN)
        {
            continue;
        }
        if (line_len < 0)
        {
            break;
        }
        if (line_len == 0)
        {
            continue;
        }
        if (strstr(line, "[FSEV-END]"))
        {
            break;
        }
    }

    /* Null-terminate so the Rust UTF-8 conversion sees a clean slice. */
    if (pos < FSEV_BUF_MAX)
    {
        fsev_buf[pos] = '\0';
    }

    ARG_UNUSED(outer_deadline);
    return (int)pos;
}

static void z2_urc_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Z2 URC reader started (Feishu events)");

    while (true)
    {
        kick_watchdog();

        if (!z2_urc_enabled)
        {
            k_sleep(K_MSEC(500));
            continue;
        }

        /* Contend politely with active AT commands. A short lock window keeps
         * worst-case latency for ATHB / ATKM / ATEP bounded. */
        if (k_mutex_lock(&z2_uart_lock, K_MSEC(50)) != 0)
        {
            k_sleep(K_MSEC(20));
            continue;
        }

        int ret = z2plus_collect_fsev_block(k_uptime_get() + 5000);

        k_mutex_unlock(&z2_uart_lock);

        if (ret <= 0)
        {
            /* 0 = no FSEV-BEGIN in this slice; <0 = collection error. */
            if (ret < 0 && ret != -EAGAIN)
            {
                LOG_WRN("FSEV collect error %d", ret);
            }
            k_sleep(K_MSEC(20));
            continue;
        }

        LOG_INF("FSEV block ready len=%d", ret);

        /* Hand off to Rust under the Rust-engine mutex. The Rust side will
         * itself acquire z2_uart_lock again for the HTTPS reply, which is why
         * we must NOT still be holding it here. */
        k_mutex_lock(&claw_lock, K_FOREVER);
        kick_watchdog();
        memset(claw_out, 0, CLAW_OUT_MAX);
        int rs = rustmcuclaw_mcu_handle_feishu_event(
                     (const uint8_t *)fsev_buf, (size_t)ret,
                     claw_out, CLAW_OUT_MAX);
        kick_watchdog();
        if (rs > 0 && claw_out[0])
        {
            serial_write_buf(claw_out);
        }
        k_mutex_unlock(&claw_lock);
    }
}
#endif /* HAVE_Z2_UART */

static void cli_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    char line[CLI_LINE_MAX];
    size_t len = 0;
    bool last_was_cr = false;

    serial_write("\r\nRustMcuClaw serial CLI started.\r\n");

    while (true)
    {
        unsigned char c;

        if (k_msgq_get(&cli_uart_rx_msgq, &c, K_MSEC(50)) != 0)
        {
            continue;
        }

        if (c == '\n' && last_was_cr)
        {
            last_was_cr = false;
            continue;
        }

        if (c == '\r' || c == '\n')
        {
            last_was_cr = (c == '\r');
#if CLI_LOCAL_ECHO
            uart_poll_out(cli_uart, '\r');
            uart_poll_out(cli_uart, '\n');
#endif
            line[len] = '\0';
            process_line(line);
            len = 0;
        }
        else if (c == 0x08 || c == 0x7f)
        {
            last_was_cr = false;
            if (len > 0)
            {
                len--;
#if CLI_LOCAL_ECHO
                serial_write("\b \b");
#endif
            }
        }
        else if (len < sizeof(line) - 1)
        {
            last_was_cr = false;
            line[len++] = (char)c;
#if CLI_LOCAL_ECHO
            uart_poll_out(cli_uart, c);
#endif
        }
    }
}

static void poll_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true)
    {
        k_sleep(K_SECONDS(1));
        k_mutex_lock(&claw_lock, K_FOREVER);
        kick_watchdog();

        /* Same heap-guard as process_line(): proactively reinit before OOM. */
        {
            size_t heap_used = rustmcuclaw_mcu_heap_used();
            size_t heap_total = rustmcuclaw_mcu_heap_size();
            if (heap_total > 0 && heap_used > heap_total * 7 / 10)
            {
                int summary_ret;

                LOG_WRN("poll: Rust heap %zu/%zu (>70%%), reinitialising", heap_used, heap_total);
                memset(claw_out, 0, CLAW_OUT_MAX);
                summary_ret = rustmcuclaw_mcu_persist_session_summary(claw_out, CLAW_OUT_MAX);
                if (summary_ret > 0 && claw_out[0] != '\0')
                {
                    LOG_INF("poll: persisted session summary before heap reset: %s", claw_out);
                }
                else if (summary_ret < 0)
                {
                    LOG_WRN("poll: failed to persist session summary before heap reset: %d",
                            summary_ret);
                }

                display_chat_push("[Memory full, history cleared]", DISPLAY_COL_SYSTEM);
                display_chat_render();
                memset(claw_out, 0, CLAW_OUT_MAX);
                (void)rustmcuclaw_mcu_init((uint8_t *)rust_heap_base,
                                           rust_heap_size,
                                           RUSTMCUCLAW_DATA_DIR,
                                           claw_out,
                                           CLAW_OUT_MAX);
            }
        }

        memset(claw_out, 0, CLAW_OUT_MAX);
        int ret = rustmcuclaw_mcu_poll(claw_out, CLAW_OUT_MAX);
        log_thread_stack_usage(k_current_get(), "claw_poll");
        kick_watchdog();
        if (ret > 0)
        {
            const char *screen_text;

            serial_write_buf(claw_out);
            /* Keep pure heartbeat traffic off-screen, but still surface any
             * scheduled-task output that may be appended after a heartbeat. */
            screen_text = display_skip_heartbeat_prefix((const char *)claw_out);
            if (screen_text && screen_text[0] != '\0')
            {
                disp_scroll_offset = 0;
                display_chat_add_text(screen_text, DISPLAY_COL_REPLY);
                display_chat_render();
            }
        }
        k_mutex_unlock(&claw_lock);
    }
}

static struct k_thread cli_thread_data;
static struct k_thread poll_thread_data;

int main(void)
{
    int ret;

    if (!device_is_ready(cli_uart))
    {
        LOG_ERR("CLI UART is not ready");
        return -ENODEV;
    }

    LOG_INF("Boot reset reason: 0x%02x", wdg_get_reset_reason());

    ret = cli_uart_irq_init();
    if (ret < 0)
    {
        LOG_ERR("CLI UART IRQ init failed: %d", ret);
        return ret;
    }
    LOG_INF("CLI UART IRQ RX enabled, queue=%u", (unsigned)CLI_UART_RX_QUEUE_LEN);

    psram_init();
    ret = init_psram_layout();
    if (ret < 0)
    {
        LOG_ERR("PSRAM layout init failed: %d", ret);
        return ret;
    }

    ret = init_display();
    if (ret < 0)
    {
        LOG_ERR("LCD init failed: %d", ret);
        return ret;
    }

    /* Boot splash: show the title bar + "Initializing..." while the
     * filesystem and Rust engine start up. */
    display_chat_push("Initializing...", DISPLAY_COL_SYSTEM);
    display_chat_render();

    disable_watchdog_and_dlps();

    k_mutex_init(&claw_lock);
    k_mutex_init(&z2_uart_lock);

#if HAVE_Z2_UART
    ret = z2_uart_irq_init();
    if (ret < 0)
    {
        LOG_ERR("Z2Plus UART IRQ init failed: %d", ret);
        return ret;
    }
    LOG_INF("Z2Plus UART IRQ RX enabled, queue=%u", (unsigned)Z2_UART_RX_QUEUE_LEN);
#endif

    kick_watchdog();
    ret = mount_data_fs();
    if (ret < 0)
    {
        return ret;
    }

    ret = seed_boot_config();
    if (ret < 0)
    {
        return ret;
    }

    /* Load GNU Unifont from SD into PSRAM for full CJK text rendering. */
    display_chat_push("Loading font...", DISPLAY_COL_SYSTEM);
    display_chat_render();
    ret = unifont_load();
    if (ret < 0)
    {
        /* Non-fatal: fall back to ASCII-only font8x8. */
        LOG_WRN("Unifont not loaded (%d), falling back to font8x8", ret);
        display_chat_push("Font unavail, ASCII only", DISPLAY_COL_SYSTEM);
    }
    else
    {
        display_chat_push("Font ready (CJK enabled)", DISPLAY_COL_SYSTEM);
    }
    display_chat_render();

    /* ---- DHT11: 上电一次读取温湿度做验证 ---- */
    {
        int32_t temp_milli_c = 0;
        int32_t humidity_milli_pct = 0;
        int dht_ret = claw_mcu_read_temp_humidity(&temp_milli_c, &humidity_milli_pct);

        if (dht_ret == 0)
        {
            char dht_msg[64];
            snprintf(dht_msg,
                     sizeof(dht_msg),
                     "DHT11: %d.%03d C  %d.%03d%%RH",
                     temp_milli_c / 1000,
                     abs(temp_milli_c % 1000),
                     humidity_milli_pct / 1000,
                     abs(humidity_milli_pct % 1000));
            display_chat_push(dht_msg, DISPLAY_COL_SYSTEM);
        }
        else if (dht_ret == -ENODEV)
        {
            display_chat_push("DHT11 not ready", DISPLAY_COL_SYSTEM);
        }
        else
        {
            display_chat_push("DHT11 read failed", DISPLAY_COL_SYSTEM);
        }
        display_chat_render();
    }
    /* ---------------------------------------- */

    /* Boot-time wall-clock sync via Z2Plus SNTP (non-fatal). The Z2 net card
     * auto-joins WiFi in its own task, so association + DHCP can take several
     * seconds after our boot point. Retry a few times with backoff and proceed
     * regardless so the boot path stays robust on offline devices. */
    {
        uint64_t epoch = 0;
        int sync_ret = -1;
        int attempt;
        const int max_attempts = 3;     /* each call iterates 5 servers */
        const int retry_delay_ms = 3000;

        display_chat_push("Syncing time...", DISPLAY_COL_SYSTEM);
        display_chat_render();

        for (attempt = 1; attempt <= max_attempts; attempt++)
        {
            kick_watchdog();
            sync_ret = z2plus_sync_internet_time(&epoch);
            kick_watchdog();
            if (sync_ret == 0)
            {
                break;
            }
            LOG_WRN("Time sync attempt %d/%d failed (%d), retrying...",
                    attempt, max_attempts, sync_ret);
            if (attempt < max_attempts)
            {
                /* Give the Z2 time to finish association/DHCP. */
                int waited = 0;
                while (waited < retry_delay_ms)
                {
                    k_sleep(K_MSEC(500));
                    kick_watchdog();
                    waited += 500;
                }
            }
        }

        if (sync_ret == 0)
        {
            char tmsg[64];
            snprintf(tmsg, sizeof(tmsg), "Time synced: %llu",
                     (unsigned long long)epoch);
            display_chat_push(tmsg, DISPLAY_COL_SYSTEM);
        }
        else
        {
            display_chat_push("Time sync skipped (offline)", DISPLAY_COL_SYSTEM);
        }
        display_chat_render();
    }

    k_mutex_lock(&claw_lock, K_FOREVER);
    kick_watchdog();
    memset(claw_out, 0, CLAW_OUT_MAX);
    ret = rustmcuclaw_mcu_init((uint8_t *)rust_heap_base,
                               rust_heap_size,
                               RUSTMCUCLAW_DATA_DIR,
                               claw_out,
                               CLAW_OUT_MAX);
    kick_watchdog();
    serial_write_buf(claw_out);
    k_mutex_unlock(&claw_lock);

    if (ret < 0)
    {
        LOG_ERR("RustMcuClaw init failed: %d", ret);
        return ret;
    }

#if HAVE_Z2_UART
    /* If `[channels.feishu] enabled = true` in rmcc.toml and both credentials
     * are present, fire `ATFL=<app_id>,<app_secret>` so the Z2Plus AT firmware
     * starts the wss long-connection and begins pushing event URCs. */
    {
        static uint8_t fs_app_id[96];
        static uint8_t fs_app_secret[128];
        static uint8_t fs_endpoint[96];

        int fs_ret = rustmcuclaw_mcu_get_feishu_creds(
                         fs_app_id, sizeof(fs_app_id),
                         fs_app_secret, sizeof(fs_app_secret),
                         fs_endpoint, sizeof(fs_endpoint));

        if (fs_ret == 1)
        {
            LOG_INF("Feishu channel enabled: app_id=%s endpoint=%s",
                    (const char *)fs_app_id, (const char *)fs_endpoint);
            display_chat_push("Feishu channel: starting...", DISPLAY_COL_SYSTEM);
            display_chat_render();
            int atfl_ret = z2plus_start_feishu_channel((const char *)fs_app_id,
                                                       (const char *)fs_app_secret);
            if (atfl_ret == 0)
            {
                LOG_INF("ATFL OK, Feishu URC reader will start");
                display_chat_push("Feishu channel: online", DISPLAY_COL_SYSTEM);
                z2_urc_enabled = true;
            }
            else
            {
                LOG_WRN("ATFL failed: %d (Feishu channel offline)", atfl_ret);
                display_chat_push("Feishu channel: offline", DISPLAY_COL_SYSTEM);
            }
            display_chat_render();
            /* Wipe credentials from SRAM scratch. */
            memset(fs_app_secret, 0, sizeof(fs_app_secret));
        }
        else
        {
            LOG_INF("Feishu channel disabled (rmcc.toml channels.feishu.enabled=false or creds empty)");
            z2_urc_enabled = false;
        }
    }
#endif

    k_thread_create(&cli_thread_data, cli_stack_area, CLI_STACK_SIZE,
                    cli_thread, NULL, NULL, NULL, CLI_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&cli_thread_data, "claw_cli");

    k_thread_create(&poll_thread_data, poll_stack_area, POLL_STACK_SIZE,
                    poll_thread, NULL, NULL, NULL, POLL_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&poll_thread_data, "claw_poll");

    k_thread_create(&touch_thread_data, touch_stack_area,
                    K_THREAD_STACK_SIZEOF(touch_stack_area),
                    touch_thread_fn, NULL, NULL, NULL,
                    TOUCH_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&touch_thread_data, "claw_touch");

#if HAVE_Z2_UART
    /* Always spawn the URC reader; it gates on `z2_urc_enabled` so the channel
     * can be toggled at runtime later without recreating threads. */
    k_thread_create(&z2_urc_thread_data, z2_urc_stack_area, Z2_URC_STACK_SIZE,
                    z2_urc_thread, NULL, NULL, NULL, Z2_URC_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&z2_urc_thread_data, "claw_z2urc");
#endif

    return 0;
}
