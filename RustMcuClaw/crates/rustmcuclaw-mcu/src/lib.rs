#![no_std]
#![allow(static_mut_refs)]

extern crate alloc;

use alloc::format;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::alloc::{GlobalAlloc, Layout};
use core::cmp::min;
use core::ffi::{c_char, CStr};
use core::fmt::Write;
use core::panic::PanicInfo;
use core::ptr;
use core::sync::atomic::{AtomicUsize, Ordering};
use font8x8::{BASIC_FONTS, UnicodeFonts};
use rustmcuclaw_common::{
    format_mcu_system_prompt, format_task_chat_input, CHAT_REQUEST_BODY_MAX,
    DEFAULT_HEARTBEAT_INTERVAL_SECS, DEFAULT_HISTORY_LIMIT, DEFAULT_MAX_TOKENS,
    DEFAULT_MAX_TOOL_LOOPS, HISTORY_CONTENT_MAX, MIN_MONITOR_INTERVAL_SECS, MIN_TASK_INTERVAL_SECS,
    SUMMARY_MEMORY_EMPTY_TEXT, SYSTEM_PROMPT_MAX,
};
use serde::{Deserialize, Serialize};

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

struct PsramBumpAllocator {
    start: AtomicUsize,
    end: AtomicUsize,
    next: AtomicUsize,
}

unsafe impl Sync for PsramBumpAllocator {}

unsafe impl GlobalAlloc for PsramBumpAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let align = layout.align().max(core::mem::align_of::<usize>());
        let size = layout.size();
        if size == 0 {
            return align as *mut u8;
        }

        let end = self.end.load(Ordering::Acquire);
        let mut current = self.next.load(Ordering::Acquire);
        loop {
            let aligned = align_up(current, align);
            let Some(new_next) = aligned.checked_add(size) else {
                return ptr::null_mut();
            };
            if new_next > end {
                return ptr::null_mut();
            }
            match self.next.compare_exchange(current, new_next, Ordering::AcqRel, Ordering::Acquire) {
                Ok(_) => return aligned as *mut u8,
                Err(actual) => current = actual,
            }
        }
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
        // The MCU assistant is a long-running single app.  PSRAM is reclaimed by
        // reboot/re-init; avoiding a free list keeps the allocator deterministic.
    }
}

#[global_allocator]
static HEAP: PsramBumpAllocator = PsramBumpAllocator {
    start: AtomicUsize::new(0),
    end: AtomicUsize::new(0),
    next: AtomicUsize::new(0),
};

fn align_up(value: usize, align: usize) -> usize {
    (value + align - 1) & !(align - 1)
}

fn truncate_utf8(value: &str, max_bytes: usize) -> String {
    if value.len() <= max_bytes {
        return value.to_string();
    }

    let mut end = max_bytes;
    while end > 0 && !value.is_char_boundary(end) {
        end -= 1;
    }

    let mut out = value[..end].to_string();
    out.push_str("...");
    out
}

extern "C" {
    fn claw_mcu_fs_read(path: *const c_char, out: *mut u8, out_cap: usize) -> isize;
    fn claw_mcu_fs_exists(path: *const c_char) -> i32;
    fn claw_mcu_fs_write(path: *const c_char, data: *const u8, data_len: usize, append: bool) -> i32;
    fn claw_mcu_fs_list(path: *const c_char, out: *mut u8, out_cap: usize) -> isize;
    fn claw_mcu_read_temp_humidity(temp_milli_c: *mut i32, humidity_milli_pct: *mut i32) -> i32;
    /// Number of board-direct LEDs declared in the overlay.
    fn claw_mcu_led_count() -> i32;
    /// Copy the human label of LED `idx` (e.g. "led1") into `out` as a
    /// null-terminated UTF-8 string. Returns 0 on success, negative errno on
    /// bad index or insufficient buffer.
    fn claw_mcu_led_name(idx: i32, out: *mut u8, out_cap: usize) -> i32;
    /// Set LED `idx` on (`1`) or off (`0`). Returns 0 on success.
    fn claw_mcu_led_set(idx: i32, on: i32) -> i32;
    /// Returns 1 if LED `idx` is on, 0 if off, negative errno on error.
    fn claw_mcu_led_get(idx: i32) -> i32;
    /// Yield-aware sleep (Zephyr `k_msleep`). Capped at 5 s on the C side.
    fn claw_mcu_sleep_ms(ms: u32);
    fn claw_mcu_https_post_json(
        endpoint: *const c_char,
        api_key: *const c_char,
        body: *const u8,
        body_len: usize,
        out: *mut u8,
        out_cap: usize,
    ) -> isize;
    fn claw_mcu_now_secs() -> u64;
    /// Look up a Unicode BMP codepoint in the PSRAM GNU Unifont bitmap.
    /// Returns 8 (halfwidth) or 16 (fullwidth) on success, 0 if absent/not loaded.
    /// `out_rows` must point to an array of 16 u16 values (bit 15 = leftmost column).
    fn claw_mcu_unifont_get_glyph(codepoint: u32, out_rows: *mut u16) -> i32;
    /// Append `text` to the on-screen chat log using RGB565 `color` and
    /// re-render. Used to mirror outgoing Feishu messages onto the board screen.
    fn claw_mcu_display_push(text: *const c_char, color: u16);
    /// WS2812B RGB strip effect engine (non-blocking; all return immediately).
    /// `duration_ms = 0` means run forever until replaced or stopped.
    fn ws2812_strip_effect_stop() -> i32;
    fn ws2812_strip_effect_solid(r: u8, g: u8, b: u8, duration_ms: u32) -> i32;
    fn ws2812_strip_effect_blink(r: u8, g: u8, b: u8, period_ms: u32, duration_ms: u32) -> i32;
    fn ws2812_strip_effect_rainbow(value: u8, frame_ms: u32, duration_ms: u32) -> i32;
}

#[derive(Clone)]
struct Config {
    data_dir: String,
    provider: ProviderConfig,
    chat: ChatConfig,
    heartbeat: HeartbeatConfig,
    files: FileConfig,
    channels: ChannelsConfig,
}

#[derive(Clone, Default)]
struct ChannelsConfig {
    feishu: FeishuConfig,
}

/// Feishu (Lark) open-platform app credentials. The `enabled` flag and
/// optional `endpoint` leave room for the future long-connection event
/// subscription loop; until that is wired up, the values are just persisted.
#[derive(Clone, Default)]
struct FeishuConfig {
    enabled: bool,
    app_id: String,
    app_secret: String,
    /// Open Lark base URL (defaults to https://open.feishu.cn when empty).
    endpoint: String,
    /// Default `chat_id` used by Rust-native monitor actions when the
    /// LLM-supplied rule leaves `chat_id` empty. Without this the
    /// `monitor_create` tool rejects rules that omit a recipient.
    default_chat_id: String,
}

#[derive(Clone)]
struct ProviderConfig {
    kind: String,
    model: String,
    endpoint: String,
    api_key: String,
    temperature: f32,
    max_tokens: u32,
}

#[derive(Clone)]
struct ChatConfig {
    history_limit: usize,
    /// Maximum number of LLM↔tool round-trips per `ask()` call. The first LLM
    /// reply counts as cycle 1; setting this to 1 disables the follow-up loop
    /// and preserves classic single-shot behaviour.
    max_tool_loops: u32,
}

#[derive(Clone)]
struct HeartbeatConfig {
    enabled: bool,
    interval_secs: u64,
}

#[derive(Clone)]
struct FileConfig {
    soul: String,
    user: String,
    role: String,
    summary_memory: String,
    tasks: String,
    monitors: String,
}

#[derive(Clone, Serialize, Deserialize)]
struct Message {
    role: String,
    content: String,
}

#[derive(Serialize)]
struct ChatRequest<'a> {
    model: &'a str,
    messages: &'a [Message],
    temperature: f32,
    max_tokens: u32,
}

#[derive(Deserialize)]
struct ChatResponse {
    choices: Vec<Choice>,
}

#[derive(Deserialize)]
struct Choice {
    message: Message,
}

#[derive(Serialize, Deserialize, Clone)]
struct SummaryMemoryRecord {
    id: String,
    created_at: String,
    summary: String,
}

#[derive(Serialize, Deserialize, Clone)]
struct Task {
    id: String,
    title: String,
    every_secs: u64,
    next_run_at: String,
    completed: bool,
}

/// Rust-native sensor monitor rule.  Once `monitor_create` registers one,
/// the `poll()` loop reads the sensor, compares against `threshold`, and
/// triggers `action_kind` entirely in Rust �?no further LLM round-trips.
#[derive(Serialize, Deserialize, Clone)]
struct Monitor {
    id: String,
    every_secs: u64,
    /// Runtime-only scheduling field. NOT serialized �?there is no reason to
    /// write a "next check time" to the SD card. On load it is always reset
    /// to `now + every_secs`. Only the rule itself and `fired` are durable.
    #[serde(skip)]
    next_check_at_secs: u64,
    /// Sensor field name. Supported: "humidity_pct", "temp_c".
    field: String,
    /// Comparison op. Supported: "gt", "lt", "gte", "lte", "eq".
    op: String,
    threshold: f32,
    /// Action kind. Supported: "feishu_send", "led_set".
    action_kind: String,
    /// For `feishu_send`: target chat_id (empty = use active session or
    /// `channels.feishu.default_chat_id`). Unused for `led_set`.
    action_chat_id: String,
    /// For `feishu_send`: message body (`{value}` = current reading).
    /// For `led_set`: ignored (state comes from `action_target_state`).
    action_message: String,
    /// For `led_set`: LED name (e.g. "led1"). Empty otherwise.
    /// `#[serde(default)]` keeps older `monitors.json` files loadable.
    #[serde(default)]
    action_led_name: String,
    /// For `led_set`: desired state ("on" | "off"). Empty otherwise.
    #[serde(default)]
    action_led_state: String,
    fire_once: bool,
    fired: bool,
}

struct App {
    config: Config,
    history: Vec<Message>,
    soul: String,
    user: String,
    role: String,
    summary_memory: String,
    tasks: Vec<Task>,
    monitors: Vec<Monitor>,
    seq: u64,
    last_heartbeat: u64,
    /// Cached Feishu tenant_access_token. Empty when never fetched or expired.
    feishu_token: String,
    /// Unix epoch (seconds) at which `feishu_token` stops being valid.
    feishu_token_expires_at: u64,
    /// Small ring of recently-handled Feishu `message_id`s for deduplication.
    feishu_recent_ids: Vec<String>,
    /// The `chat_id` of the most recent Feishu message received by Claw.
    /// Used as the default recipient for monitor actions so the user never
    /// has to configure a chat_id manually: if you told Claw via Feishu,
    /// Claw already knows where to reply.
    active_feishu_chat_id: String,
}

static mut APP: Option<App> = None;

const DEFAULT_CONFIG: &str = concat!(
    "data_dir = \"/RAM:/RustMcuClaw\"\n\n",
    "[provider]\n",
    "kind = \"mock\"\n",
    "model = \"mock-local\"\n",
    "endpoint = \"https://api.openai.com/v1/chat/completions\"\n",
    "api_key = \"\"\n",
    "temperature = 0.7\n",
    "max_tokens = 2048\n\n",
    "[chat]\n",
    "history_limit = 12\n",
    "max_tool_loops = 4\n\n",
    "[heartbeat]\n",
    "enabled = true\n",
    "interval_secs = 60\n\n",
    "[files]\n",
    "soul = \"soul.md\"\n",
    "user = \"user.md\"\n",
    "role = \"role.md\"\n",
    "summary_memory = \"summary_memory.jsonl\"\n",
    "tasks = \"tasks.json\"\n",
    "monitors = \"monitors.json\"\n\n",
    "[channels.feishu]\n",
    "# Feishu / Lark open-platform app. Long-connection event subscription is WIP.\n",
    "# App Secret is sensitive �?write the real value into the SD card copy of\n",
    "# rmcc.toml manually; the firmware never embeds it.\n",
    "enabled = false\n",
    "app_id = \"cli_a92512f2f3391bd4\"\n",
    "app_secret = \"\"\n",
    "endpoint = \"https://open.feishu.cn\"\n",
    "# Default recipient used by Rust-native monitor rules when the LLM\n",
    "# leaves the rule's chat_id empty. Fill this in to make `\"give me a\n",
    "# Feishu message\"` style natural-language requests work without the\n",
    "# LLM having to guess a chat_id.\n",
    "default_chat_id = \"\"\n",
);

const DEFAULT_SOUL: &str = "你是 Claw，一个运行在 RTL8783G EVB 上的串口智能体。\n";
const DEFAULT_USER: &str = "用户通过 PC 串口工具和你聊天。回答要简洁、可靠。\n";
const DEFAULT_ROLE: &str = "你具备长期总结记忆、定时任务和云端 LLM 对话能力。\n";

#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_init(
    heap_ptr: *mut u8,
    heap_len: usize,
    data_dir: *const c_char,
    out: *mut u8,
    out_cap: usize,
) -> i32 {
    if heap_ptr.is_null() || heap_len < 64 * 1024 {
        write_out(out, out_cap, "ERR: PSRAM heap is missing or too small\r\n");
        return -1;
    }

    let start = align_up(heap_ptr as usize, core::mem::align_of::<usize>());
    let end = (heap_ptr as usize).saturating_add(heap_len);
    HEAP.start.store(start, Ordering::Release);
    HEAP.next.store(start, Ordering::Release);
    HEAP.end.store(end, Ordering::Release);

    let dir_override = cstr_to_string(data_dir).unwrap_or_default();
    let mut config = Config::default();
    if !dir_override.is_empty() {
        config.data_dir = dir_override.clone();
    }

    let cfg_path = join(&config.data_dir, "rmcc.toml");
    if path_exists(&cfg_path) {
        if let Ok(text) = read_text(&cfg_path) {
            config = parse_config(&text, config);
            if !dir_override.is_empty() {
                config.data_dir = dir_override;
            }
        }
    } else {
        let _ = write_text(&cfg_path, DEFAULT_CONFIG, false);
    }

    let _ = ensure_file(&join(&config.data_dir, &config.files.soul), DEFAULT_SOUL);
    let _ = ensure_file(&join(&config.data_dir, &config.files.user), DEFAULT_USER);
    let _ = ensure_file(&join(&config.data_dir, &config.files.role), DEFAULT_ROLE);
    let _ = ensure_file(&join(&config.data_dir, &config.files.summary_memory), "");
    let _ = ensure_file(&join(&config.data_dir, &config.files.tasks), "[]\n");
    let _ = ensure_file(&join(&config.data_dir, &config.files.monitors), "[]\n");

    let soul_path = join(&config.data_dir, &config.files.soul);
    let user_path = join(&config.data_dir, &config.files.user);
    let role_path = join(&config.data_dir, &config.files.role);
    let summary_path = join(&config.data_dir, &config.files.summary_memory);
    let tasks_path = join(&config.data_dir, &config.files.tasks);
    let monitors_path = join(&config.data_dir, &config.files.monitors);

    APP = Some(App {
        soul: read_text(&soul_path).unwrap_or_else(|_| DEFAULT_SOUL.to_string()),
        user: read_text(&user_path).unwrap_or_else(|_| DEFAULT_USER.to_string()),
        role: read_text(&role_path).unwrap_or_else(|_| DEFAULT_ROLE.to_string()),
        summary_memory: read_text(&summary_path).unwrap_or_default(),
        tasks: load_tasks_from_path(&tasks_path),
        monitors: load_monitors_from_path(&monitors_path),
        config,
        history: Vec::new(),
        seq: 0,
        last_heartbeat: now_secs(),
        feishu_token: String::new(),
        feishu_token_expires_at: 0,
        feishu_recent_ids: Vec::new(),
        active_feishu_chat_id: String::new(),
    });

    write_out(out, out_cap, "RustMcuClaw MCU ready. Type /help for commands.\r\nclaw> ");
    0
}

#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_handle_line(
    line: *const c_char,
    out: *mut u8,
    out_cap: usize,
) -> i32 {
    let Some(input) = cstr_to_string(line) else {
        write_out(out, out_cap, "ERR: invalid input\r\nclaw> ");
        return -1;
    };

    let app = match APP.as_mut() {
        Some(app) => app,
        None => {
            write_out(out, out_cap, "ERR: RustMcuClaw is not initialized\r\n");
            return -2;
        }
    };

    let response = app.handle_line(input.trim());
    write_out(out, out_cap, &response);
    0
}

#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_poll(out: *mut u8, out_cap: usize) -> i32 {
    let app = match APP.as_mut() {
        Some(app) => app,
        None => return -1,
    };

    let response = app.poll();
    if response.is_empty() {
        return 0;
    }
    write_out(out, out_cap, &response);
    response.len() as i32
}

#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_persist_session_summary(
    out: *mut u8,
    out_cap: usize,
) -> i32 {
    let app = match APP.as_mut() {
        Some(app) => app,
        None => {
            write_out(out, out_cap, "ERR: RustMcuClaw is not initialized\r\n");
            return -2;
        }
    };

    let note = app.persist_session_summary();
    app.history.clear();

    write_out(out, out_cap, &note);
    note.len() as i32
}

/// Returns the current Feishu channel credentials to the C bootstrap so it can
/// auto-issue `ATFL=<app_id>,<app_secret>` to the Z2Plus AT firmware after
/// `rustmcuclaw_mcu_init`. Returns 1 when the channel is enabled and both
/// credentials are present, 0 when disabled or incomplete, -1 if the Rust
/// engine is not yet initialised. On success the caller-provided buffers
/// receive null-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_get_feishu_creds(
    app_id_out: *mut u8,
    app_id_cap: usize,
    app_secret_out: *mut u8,
    app_secret_cap: usize,
    endpoint_out: *mut u8,
    endpoint_cap: usize,
) -> i32 {
    let app = match APP.as_ref() {
        Some(app) => app,
        None => return -1,
    };
    let fs = &app.config.channels.feishu;
    if !app_id_out.is_null() && app_id_cap > 0 {
        write_out(app_id_out, app_id_cap, &fs.app_id);
    }
    if !app_secret_out.is_null() && app_secret_cap > 0 {
        write_out(app_secret_out, app_secret_cap, &fs.app_secret);
    }
    if !endpoint_out.is_null() && endpoint_cap > 0 {
        let ep = if fs.endpoint.is_empty() { "https://open.feishu.cn" } else { fs.endpoint.as_str() };
        write_out(endpoint_out, endpoint_cap, ep);
    }
    if fs.enabled && !fs.app_id.is_empty() && !fs.app_secret.is_empty() {
        1
    } else {
        0
    }
}

/// Consume a Feishu event block forwarded from the Z2Plus AT URC reader.
/// The block layout is the one emitted by `z2_feishu_ws.c::emit_event_urc`,
/// repacked by the MCU URC collector as:
///   `[FSEV-BEGIN]\n` header `key=value\n` lines including `payload_len=N\n`,
///   then exactly `N` raw bytes of UTF-8 JSON payload, then `[FSEV-END]`.
///
/// On success the function may invoke `claw_mcu_https_post_json` to reply to
/// the originating Feishu chat. `out`/`out_cap` receives a short human
/// readable status string for logging on the MCU serial console.
#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_handle_feishu_event(
    block_ptr: *const u8,
    block_len: usize,
    out: *mut u8,
    out_cap: usize,
) -> i32 {
    if block_ptr.is_null() || block_len == 0 {
        write_out(out, out_cap, "[fsev] empty block\r\n");
        return -1;
    }
    let app = match APP.as_mut() {
        Some(app) => app,
        None => {
            write_out(out, out_cap, "[fsev] not initialised\r\n");
            return -2;
        }
    };
    let block = core::slice::from_raw_parts(block_ptr, block_len);
    let status = app.handle_feishu_event(block);
    let mut text = status.unwrap_or_else(|| "[fsev] dropped".to_string());
    if !text.ends_with('\n') {
        text.push_str("\r\n");
    }
    write_out(out, out_cap, &text);
    text.len() as i32
}

/// Returns the number of bytes currently consumed from the PSRAM bump allocator.
#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_heap_used() -> usize {
    let next = HEAP.next.load(Ordering::Acquire);
    let start = HEAP.start.load(Ordering::Acquire);
    next.saturating_sub(start)
}

/// Returns the total size of the PSRAM bump allocator in bytes.
#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_heap_size() -> usize {
    let end = HEAP.end.load(Ordering::Acquire);
    let start = HEAP.start.load(Ordering::Acquire);
    end.saturating_sub(start)
}

/// Draw text into a caller-provided RGB565 framebuffer using the built-in
/// `font8x8` bitmap font.  No heap allocation is performed.
///
/// * `pixels`  �?pointer to the RGB565 framebuffer (row-major, `width × height` `u16` words).
/// * `width`   �?framebuffer width in pixels.
/// * `height`  �?framebuffer height in pixels.
/// * `x`, `y` �?top-left text origin (may be 0).
/// * `text`    �?null-terminated UTF-8 string to render.
/// * `fg`      �?foreground colour in RGB565.
/// * `scale`   �?pixel magnification: 1 �?8×8 per glyph, 2 �?16×16, etc.
///
/// Returns the number of glyphs rendered, or a negative value on bad input.
#[no_mangle]
pub unsafe extern "C" fn rustmcuclaw_mcu_draw_text_rgb565(
    pixels: *mut u16,
    width: u16,
    height: u16,
    x: i32,
    y: i32,
    text: *const c_char,
    fg: u16,
    scale: u8,
) -> i32 {
    if pixels.is_null() || width == 0 || height == 0 || text.is_null() || scale == 0 {
        return -1;
    }

    // Zero-allocation: borrow the C string bytes and reinterpret as UTF-8.
    // from_utf8 never allocates; it just validates the byte slice.
    let bytes = core::ffi::CStr::from_ptr(text).to_bytes();
    let text_str = core::str::from_utf8(bytes).unwrap_or("");

    let w  = width  as i32;
    let h  = height as i32;
    let sc = scale  as i32;
    let mut cursor_x = x;
    let mut cursor_y = y;
    let mut count = 0i32;

    for ch in text_str.chars() {
        if ch == '\n' {
            cursor_x = x;
            // Use the taller unifont line height when possible; if the PSRAM
            // font is not yet loaded we still get the correct advance.
            cursor_y += 16 * sc;
            continue;
        }
        if ch == '\r' {
            continue;
        }

        // ── Try PSRAM GNU Unifont first (supports CJK and all BMP chars) ──
        let mut rows = [0u16; 16];
        let glyph_w = claw_mcu_unifont_get_glyph(ch as u32, rows.as_mut_ptr());
        if glyph_w > 0 {
            let gw = glyph_w as i32;
            for row in 0..16usize {
                let row_bits = rows[row];
                if row_bits == 0 { continue; }
                for col in 0..gw as usize {
                    // Bit 15 is always the leftmost pixel.
                    // Halfwidth (gw=8): pixels in bits [15:8]; col 0→bit15, col 7→bit8.
                    // Fullwidth (gw=16): pixels in bits [15:0]; col 0→bit15, col15→bit0.
                    if (row_bits >> (15 - col as i32)) & 1 == 0 { continue; }
                    let bx = cursor_x + col as i32 * sc;
                    let by = cursor_y + row  as i32 * sc;
                    for dy in 0..sc {
                        for dx in 0..sc {
                            let px = bx + dx;
                            let py = by + dy;
                            if px >= 0 && py >= 0 && px < w && py < h {
                                *pixels.add((py * w + px) as usize) = fg;
                            }
                        }
                    }
                }
            }
            cursor_x += gw * sc;
            count += 1;
            continue;
        }

        // ── Fall back to font8x8 for ASCII / Latin when unifont is absent ──
        if !ch.is_ascii() {
            // Non-ASCII glyph not in PSRAM font �?advance one halfwidth cell.
            cursor_x += 8 * sc;
            continue;
        }
        let Some(glyph) = BASIC_FONTS.get(ch) else {
            cursor_x += 8 * sc;
            continue;
        };
        // font8x8 glyphs are 8×8; render at the top of the 16-px slot.
        for row in 0..8usize {
            let bits = glyph[row];
            if bits == 0 { continue; }
            for col in 0..8usize {
                if (bits >> col) & 1 == 0 { continue; }
                let bx = cursor_x + col as i32 * sc;
                let by = cursor_y + row as i32 * sc;
                for dy in 0..sc {
                    for dx in 0..sc {
                        let px = bx + dx;
                        let py = by + dy;
                        if px >= 0 && py >= 0 && px < w && py < h {
                            *pixels.add((py * w + px) as usize) = fg;
                        }
                    }
                }
            }
        }
        cursor_x += 8 * sc;
        count += 1;
    }

    count
}

impl Default for Config {
    fn default() -> Self {
        Self {
            data_dir: "/RAM:/RustMcuClaw".to_string(),
            provider: ProviderConfig {
                kind: "mock".to_string(),
                model: "mock-local".to_string(),
                endpoint: "https://api.openai.com/v1/chat/completions".to_string(),
                api_key: String::new(),
                temperature: 0.7,
                max_tokens: DEFAULT_MAX_TOKENS,
            },
            chat: ChatConfig {
                history_limit: DEFAULT_HISTORY_LIMIT,
                max_tool_loops: DEFAULT_MAX_TOOL_LOOPS,
            },
            heartbeat: HeartbeatConfig { enabled: true, interval_secs: DEFAULT_HEARTBEAT_INTERVAL_SECS },
            files: FileConfig {
                soul: "soul.md".to_string(),
                user: "user.md".to_string(),
                role: "role.md".to_string(),
                summary_memory: "summary_memory.jsonl".to_string(),
                tasks: "tasks.json".to_string(),
                monitors: "monitors.json".to_string(),
            },
            channels: ChannelsConfig::default(),
        }
    }
}

impl App {
    fn handle_line(&mut self, line: &str) -> String {
        if line.is_empty() {
            return "claw> ".to_string();
        }

        if line.starts_with('/') {
            return self.handle_command(line);
        }

        let answer = self.ask(line);
        format!("{answer}\r\nclaw> ")
    }

    fn handle_command(&mut self, line: &str) -> String {
        let mut parts = line.split_whitespace();
        let cmd = parts.next().unwrap_or("");
        match cmd {
            "/help" => concat!(
                "Commands:\r\n",
                "  /help                 show help\r\n",
                "  /doctor               show MCU config/status\r\n",
                "  /task list            list tasks\r\n",
                "  /task add <title> every <secs>  (minimum 30s)\r\n",
                "  /task done <id-prefix>\r\n",
                "  /exit                 summarize current serial session\r\n",
                "Any other line is sent to the cloud LLM.\r\nclaw> "
            ).to_string(),
            "/doctor" => self.doctor(),
            "/exit" => {
                let note = self.persist_session_summary();
                self.history.clear();
                format!("Memory saved: {}\r\nSerial assistant remains running.\r\nclaw> ", note)
            }
            "/task" => match parts.next() {
                Some("list") => self.task_list(),
                Some("add") => self.task_add(line),
                Some("done") => self.task_done(parts.next().unwrap_or("")),
                _ => "Usage: /task list | /task add <title> every <secs> (minimum 30s) | /task done <id-prefix>\r\nclaw> ".to_string(),
            },
            _ => "Unknown command. Type /help.\r\nclaw> ".to_string(),
        }
    }

    fn ask(&mut self, user_input: &str) -> String {
        // Push the user message into history once. Each loop cycle then
        // rebuilds the LLM request from the up-to-date history so the model
        // sees prior assistant replies and [tool_results] turns.
        self.history.push(Message {
            role: "user".to_string(),
            content: user_input.to_string(),
        });

        let max_loops = self.config.chat.max_tool_loops.max(1) as usize;
        let mut display = String::new();
        let mut cycle: usize = 0;
        let stop_reason;

        loop {
            cycle += 1;
            let messages = self.bounded_messages_for_chat();
            let raw_answer = self.chat(&messages).unwrap_or_else(|err| format!("[LLM error] {err}"));
            let tool_calls = parse_tool_calls(&raw_answer);
            let cleaned = strip_tool_calls(&raw_answer, &tool_calls);

            // Record the assistant turn before deciding whether to loop, so
            // history stays consistent even if we break early.
            self.history.push(Message {
                role: "assistant".to_string(),
                content: raw_answer,
            });

            // Append the assistant's natural-language portion to the
            // user-visible display (with a cycle marker after the first).
            if !cleaned.trim().is_empty() {
                if !display.is_empty() {
                    let _ = write!(display, "\r\n--- assistant (cycle {}) ---\r\n", cycle);
                }
                display.push_str(cleaned.trim_end());
            } else if !display.is_empty() {
                let _ = write!(display, "\r\n--- assistant (cycle {}) ---\r\n(no narration)", cycle);
            }

            if tool_calls.is_empty() {
                stop_reason = None;
                break;
            }

            // Execute each tool call in order and aggregate their results.
            let mut results = String::new();
            for call in &tool_calls {
                let result = self.execute_tool(&call.name, &call.arguments);
                let _ = writeln!(results, "[tool {}] {}", call.name, result);
            }
            let results_trim = results.trim_end().to_string();

            let _ = write!(display, "\r\n--- tool results (cycle {}) ---\r\n{}", cycle, results_trim);

            // Feed the tool output back to the LLM as a user-role turn so the
            // next cycle (or future questions) can reason about it.
            self.history.push(Message {
                role: "user".to_string(),
                content: format!("[tool_results]\n{}", results_trim),
            });

            if cycle >= max_loops {
                stop_reason = Some(format!(
                    "[note] reached max_tool_loops={} without a final answer; stopping. Send another message to continue.",
                    max_loops
                ));
                break;
            }
            // Otherwise: loop again so the LLM can read the tool results and
            // either summarise or chain another tool call.
        }

        if let Some(note) = stop_reason {
            if !display.is_empty() {
                display.push_str("\r\n");
            }
            display.push_str(&note);
        }

        // Trim the rolling history to the configured window. Each "logical"
        // round can produce up to 4 messages (user, assistant, tool_results,
        // assistant), so we keep a slightly larger buffer.
        let max_messages = self.config.chat.history_limit.saturating_mul(4);
        if self.history.len() > max_messages {
            let drain = self.history.len() - max_messages;
            self.history.drain(0..drain);
        }

        display
    }

    /// Build the message list for a chat request directly from the current
    /// rolling `history` (which already includes the latest user input or
    /// [tool_results] turn). Used by `ask()` so a single user message can
    /// drive multiple LLM round-trips without duplicating the user turn.
    fn bounded_messages_for_chat(&self) -> Vec<Message> {
        let max_history_messages = self.config.chat.history_limit.saturating_mul(4).max(2);
        let mut history: Vec<Message> = self.history
            .iter()
            .rev()
            .take(max_history_messages)
            .map(|msg| Message {
                role: msg.role.clone(),
                content: truncate_utf8(&msg.content, HISTORY_CONTENT_MAX),
            })
            .collect();
        history.reverse();

        let mut messages = Vec::new();
        messages.push(Message {
            role: "system".to_string(),
            content: truncate_utf8(&self.system_prompt(), SYSTEM_PROMPT_MAX),
        });
        messages.extend(history);

        while messages.len() > 2 && self.encoded_chat_len(&messages).unwrap_or(usize::MAX) > CHAT_REQUEST_BODY_MAX {
            messages.remove(1);
        }
        messages
    }

    fn encoded_chat_len(&self, messages: &[Message]) -> Result<usize, ()> {
        let req = ChatRequest {
            model: &self.config.provider.model,
            messages,
            temperature: self.config.provider.temperature,
            max_tokens: self.config.provider.max_tokens,
        };

        serde_json::to_vec(&req).map(|body| body.len()).map_err(|_| ())
    }

    fn chat(&self, messages: &[Message]) -> Result<String, String> {
        if self.config.provider.kind == "mock" {
            let last = messages.iter().rev().find(|m| m.role == "user").map(|m| m.content.as_str()).unwrap_or("");
            return Ok(format!("[mock-mcu:{}] {}", self.config.provider.model, last));
        }

        if self.config.provider.endpoint.is_empty() {
            return Err("provider.endpoint is empty in rmcc.toml".to_string());
        }

        let req = ChatRequest {
            model: &self.config.provider.model,
            messages,
            temperature: self.config.provider.temperature,
            max_tokens: self.config.provider.max_tokens,
        };
        let body = serde_json::to_vec(&req).map_err(|_| "failed to encode chat request".to_string())?;
        if body.len() > CHAT_REQUEST_BODY_MAX {
            return Err(format!("chat request too large for Z2Plus AT link: {} bytes", body.len()));
        }
        let mut response = vec_with_len(64 * 1024);
        let endpoint = c_string(&self.config.provider.endpoint);
        let api_key = c_string(&self.config.provider.api_key);
        let ret = unsafe {
            claw_mcu_https_post_json(
                endpoint.as_ptr() as *const c_char,
                api_key.as_ptr() as *const c_char,
                body.as_ptr(),
                body.len(),
                response.as_mut_ptr(),
                response.len(),
            )
        };
        if ret < 0 {
            return Err(format!("z2plus HTTPS failed: {ret}"));
        }
        response.truncate(ret as usize);
        let parsed: ChatResponse = serde_json::from_slice(&response).map_err(|_| "invalid chat response JSON".to_string())?;
        parsed.choices.first()
            .map(|c| c.message.content.clone())
            .filter(|s| !s.is_empty())
            .ok_or_else(|| "chat response has no content".to_string())
    }

    fn system_prompt(&self) -> String {
        let memory = self.render_memory();
        format_mcu_system_prompt(&self.soul, &self.user, &self.role, &memory)
    }

    fn render_memory(&self) -> String {
        let mut out = String::new();
        for line in self.summary_memory.lines().rev().take(8) {
            if let Ok(record) = serde_json::from_str::<SummaryMemoryRecord>(line) {
                let _ = writeln!(out, "- [{}] {}", record.created_at, record.summary);
            }
        }
        if out.is_empty() {
            format!("- {}\n", SUMMARY_MEMORY_EMPTY_TEXT)
        } else {
            out
        }
    }

    fn persist_session_summary(&mut self) -> String {
        if self.history.is_empty() {
            return "(no history to summarize)".to_string();
        }

        // Build a compact history snippet to send to LLM
        let mut raw = String::new();
        for msg in self.history.iter().rev().take(10).rev() {
            let snippet = truncate_utf8(&msg.content, 200);
            let _ = writeln!(raw, "{}: {}", msg.role, snippet);
        }

        // Ask LLM to produce a short memory note (1-2 sentences)
        let prompt_msgs = [
            Message {
                role: "system".to_string(),
                content: "You are a memory assistant. Summarize the following conversation into ONE compact sentence (max 120 chars) for long-term memory. English only. No preamble.".to_string(),
            },
            Message {
                role: "user".to_string(),
                content: raw.clone(),
            },
        ];
        let summary = self.chat(&prompt_msgs).unwrap_or_else(|_| {
            // Fallback: raw truncated dump
            truncate_utf8(&raw, 200)
        });
        let summary = truncate_utf8(&summary, 200);

        let record = SummaryMemoryRecord {
            id: self.next_id(),
            created_at: rfc3339(now_secs()),
            summary: summary.clone(),
        };
        if let Ok(mut line) = serde_json::to_string(&record) {
            line.push('\n');
            self.summary_memory.push_str(&line);
            let _ = write_text(&join(&self.config.data_dir, &self.config.files.summary_memory), &line, true);
        }
        summary
    }

    fn doctor(&self) -> String {
        let fs = &self.config.channels.feishu;
        let fs_endpoint = if fs.endpoint.is_empty() { "https://open.feishu.cn" } else { fs.endpoint.as_str() };
        let default_chat = if fs.default_chat_id.is_empty() { "<unset>" } else { fs.default_chat_id.as_str() };
        format!(
            "RustMcuClaw MCU doctor:\r\n  data_dir: {}\r\n  provider: {} / {}\r\n  endpoint: {}\r\n  history_limit: {}\r\n  max_tool_loops: {}\r\n  heartbeat: {} / {}s\r\n  heap: PSRAM bump allocator\r\n  fs: PSRAM FATFS via Zephyr FS callbacks\r\n  net: Z2Plus HTTPS callback\r\n  channel.feishu: {} app_id={} secret={} endpoint={} default_chat_id={}\r\n  monitors: {} active\r\nclaw> ",
            self.config.data_dir,
            self.config.provider.kind,
            self.config.provider.model,
            self.config.provider.endpoint,
            self.config.chat.history_limit,
            self.config.chat.max_tool_loops,
            if self.config.heartbeat.enabled { "enabled" } else { "disabled" },
            self.config.heartbeat.interval_secs,
            if fs.enabled { "enabled" } else { "disabled" },
            if fs.app_id.is_empty() { "<unset>" } else { fs.app_id.as_str() },
            mask_secret(&fs.app_secret),
            fs_endpoint,
            default_chat,
            self.monitors.iter().filter(|m| !(m.fire_once && m.fired)).count(),
        )
    }

    /// Parse a Feishu event block forwarded by the AT URC reader and, when it
    /// is a text message in `im.message.receive_v1`, run the user text through
    /// the same LLM pipeline as the serial CLI (`ask()`) and POST the answer
    /// back to the originating chat via `/open-apis/im/v1/messages`.
    ///
    /// Returns a short status string for the MCU log; `None` means "ignored".
    fn handle_feishu_event(&mut self, block: &[u8]) -> Option<String> {
        let s = core::str::from_utf8(block).ok()?;
        let plen_key = "payload_len=";
        let plen_idx = s.find(plen_key)?;
        let after_eq = &s[plen_idx + plen_key.len()..];
        let nl = after_eq.find('\n')?;
        let plen: usize = after_eq[..nl].trim().parse().ok()?;
        let payload_start = plen_idx + plen_key.len() + nl + 1;
        if payload_start + plen > s.len() {
            return Some(format!("[fsev] truncated payload need={} have={}", plen, s.len() - payload_start));
        }
        let payload = &s[payload_start..payload_start + plen];

        let v: serde_json::Value = match serde_json::from_str(payload) {
            Ok(v) => v,
            Err(_) => return Some("[fsev] payload not JSON".to_string()),
        };

        let event_type = v.pointer("/header/event_type").and_then(|x| x.as_str()).unwrap_or("");
        if event_type != "im.message.receive_v1" {
            return Some(format!("[fsev skip] {}", event_type));
        }

        let message_id = v.pointer("/event/message/message_id").and_then(|x| x.as_str()).unwrap_or("");
        if message_id.is_empty() {
            return Some("[fsev] missing message_id".to_string());
        }
        if self.feishu_seen(message_id) {
            return Some(format!("[fsev dup] {}", message_id));
        }
        self.feishu_remember(message_id);

        let chat_id = match v.pointer("/event/message/chat_id").and_then(|x| x.as_str()) {
            Some(s) if !s.is_empty() => s.to_string(),
            _ => return Some("[fsev] missing chat_id".to_string()),
        };
        // Remember this chat so Rust-native monitor rules can reach the user
        // without requiring a manually-configured default_chat_id.
        self.active_feishu_chat_id = chat_id.clone();

        let msg_type = v.pointer("/event/message/message_type").and_then(|x| x.as_str()).unwrap_or("");
        if msg_type != "text" {
            let note = format!("（暂只支持文本消息，收到 {}）", msg_type);
            let _ = self.feishu_send_text(&chat_id, &note);
            return Some(format!("[fsev skip-type] {}", msg_type));
        }
        let content_str = v.pointer("/event/message/content").and_then(|x| x.as_str()).unwrap_or("");
        let inner: serde_json::Value = match serde_json::from_str(content_str) {
            Ok(v) => v,
            Err(_) => return Some("[fsev] content not JSON".to_string()),
        };
        let text = inner.get("text").and_then(|x| x.as_str()).unwrap_or("").trim();
        let cleaned = strip_feishu_mention(text);
        if cleaned.is_empty() {
            return Some("[fsev] empty text".to_string());
        }

        // Mirror the inbound Feishu user message onto the board screen the same
        // way the serial CLI shows local input: "> <text>" in user-cyan
        // (DISPLAY_COL_USER = 0x07FF).
        let in_line = format!("> {}", cleaned);
        let in_c = c_string(&in_line);
        unsafe {
            claw_mcu_display_push(in_c.as_ptr() as *const c_char, 0x07FF);
        }

        let reply = self.ask(&cleaned);
        // Strip the trailing "claw> " prompt suffix; `ask()` doesn't emit it
        // but a future refactor might. Also Feishu has a per-message size cap,
        // so trim long replies.
        let trimmed = reply.trim_end().trim_end_matches("claw>").trim_end();
        let send_text = truncate_utf8(trimmed, 1500);

        match self.feishu_send_text(&chat_id, &send_text) {
            Ok(()) => Some(format!("[fsev ok] chat={} reply_len={}", chat_id, send_text.len())),
            Err(e) => Some(format!("[fsev send-err] {}", e)),
        }
    }

    fn feishu_seen(&self, message_id: &str) -> bool {
        self.feishu_recent_ids.iter().any(|s| s == message_id)
    }

    fn feishu_remember(&mut self, message_id: &str) {
        self.feishu_recent_ids.push(message_id.to_string());
        if self.feishu_recent_ids.len() > 8 {
            self.feishu_recent_ids.remove(0);
        }
    }

    fn feishu_endpoint_base(&self) -> &str {
        let ep = self.config.channels.feishu.endpoint.as_str();
        if ep.is_empty() { "https://open.feishu.cn" } else { ep }
    }

    /// Fetch (and cache) a Feishu tenant_access_token. Token TTL is honoured
    /// with a 60-second safety margin so we re-issue before the cloud rejects.
    fn feishu_ensure_token(&mut self) -> Result<(), String> {
        let now = now_secs();
        if !self.feishu_token.is_empty() && now + 60 < self.feishu_token_expires_at {
            return Ok(());
        }
        let app_id = self.config.channels.feishu.app_id.clone();
        let app_secret = self.config.channels.feishu.app_secret.clone();
        if app_id.is_empty() || app_secret.is_empty() {
            return Err("feishu app_id/app_secret missing".to_string());
        }
        let url = format!("{}/open-apis/auth/v3/tenant_access_token/internal", self.feishu_endpoint_base());
        let body = format!(
            "{{\"app_id\":\"{}\",\"app_secret\":\"{}\"}}",
            json_escape(&app_id),
            json_escape(&app_secret)
        );
        let url_c = c_string(&url);
        let key_c = c_string("");
        let mut resp = vec_with_len(2048);
        let ret = unsafe {
            claw_mcu_https_post_json(
                url_c.as_ptr() as *const c_char,
                key_c.as_ptr() as *const c_char,
                body.as_bytes().as_ptr(),
                body.len(),
                resp.as_mut_ptr(),
                resp.len(),
            )
        };
        if ret < 0 {
            return Err(format!("token http {}", ret));
        }
        resp.truncate(ret as usize);
        let v: serde_json::Value = serde_json::from_slice(&resp)
            .map_err(|_| "token response not JSON".to_string())?;
        let code = v.get("code").and_then(|x| x.as_i64()).unwrap_or(-1);
        if code != 0 {
            return Err(format!("token api code={}", code));
        }
        let token = v.get("tenant_access_token").and_then(|x| x.as_str())
            .ok_or_else(|| "missing tenant_access_token".to_string())?;
        let expire = v.get("expire").and_then(|x| x.as_u64()).unwrap_or(7200);
        self.feishu_token = token.to_string();
        self.feishu_token_expires_at = now.saturating_add(expire);
        Ok(())
    }

    /// Send a text message to the given Feishu chat. Requires a valid token,
    /// which is refreshed transparently.
    fn feishu_send_text(&mut self, chat_id: &str, text: &str) -> Result<(), String> {
        self.feishu_ensure_token()?;
        let url = format!("{}/open-apis/im/v1/messages?receive_id_type=chat_id", self.feishu_endpoint_base());
        // content is a JSON-string field (a JSON value encoded as a string).
        let content_inner = format!("{{\"text\":\"{}\"}}", json_escape(text));
        let body = format!(
            "{{\"receive_id\":\"{}\",\"msg_type\":\"text\",\"content\":\"{}\"}}",
            json_escape(chat_id),
            json_escape(&content_inner)
        );
        let url_c = c_string(&url);
        let auth_c = c_string(&self.feishu_token);
        let mut resp = vec_with_len(4096);
        let ret = unsafe {
            claw_mcu_https_post_json(
                url_c.as_ptr() as *const c_char,
                auth_c.as_ptr() as *const c_char,
                body.as_bytes().as_ptr(),
                body.len(),
                resp.as_mut_ptr(),
                resp.len(),
            )
        };
        if ret < 0 {
            return Err(format!("send http {}", ret));
        }
        resp.truncate(ret as usize);
        let v: serde_json::Value = serde_json::from_slice(&resp)
            .map_err(|_| "send response not JSON".to_string())?;
        let code = v.get("code").and_then(|x| x.as_i64()).unwrap_or(-1);
        if code != 0 {
            // Force token refresh on auth errors so the next call retries fresh.
            if code == 99991663 || code == 99991664 || code == 99991661 {
                self.feishu_token.clear();
                self.feishu_token_expires_at = 0;
            }
            return Err(format!("send api code={}", code));
        }
        // Mirror the outgoing Feishu message onto the board screen so the user
        // can see what the agent pushed to the chat. Pale-cyan (DISPLAY_COL_SYSTEM).
        let screen_line = format!("→飞书: {}", text);
        let line_c = c_string(&screen_line);
        unsafe {
            claw_mcu_display_push(line_c.as_ptr() as *const c_char, 0x9EFB);
        }
        Ok(())
    }

    fn save_tasks(&self) {
        if let Ok(mut text) = serde_json::to_string_pretty(&self.tasks) {
            text.push('\n');
            let _ = write_text(&join(&self.config.data_dir, &self.config.files.tasks), &text, false);
        }
    }

    fn save_monitors(&self) {
        if let Ok(mut text) = serde_json::to_string_pretty(&self.monitors) {
            text.push('\n');
            let _ = write_text(&join(&self.config.data_dir, &self.config.files.monitors), &text, false);
        }
    }

    fn task_list(&self) -> String {
        if self.tasks.is_empty() {
            return "No tasks.\r\nclaw> ".to_string();
        }
        let mut out = String::new();
        for task in &self.tasks {
            let state = if task.completed { "done" } else { "open" };
            let _ = writeln!(out, "{} [{}] every {}s next {} - {}", task.id, state, task.every_secs, task.next_run_at, task.title);
        }
        out.push_str("claw> ");
        out.replace('\n', "\r\n")
    }

    fn task_add(&mut self, line: &str) -> String {
        let body = line.trim_start_matches("/task add").trim();
        let Some(idx) = body.rfind(" every ") else {
            return "Usage: /task add <title> every <secs> (minimum 30s)\r\nclaw> ".to_string();
        };
        let title = body[..idx].trim();
        let requested_secs = body[idx + 7..]
            .trim()
            .parse::<u64>()
            .unwrap_or(0);
        let secs = requested_secs.max(MIN_TASK_INTERVAL_SECS);
        if title.is_empty() {
            return "Task title is empty.\r\nclaw> ".to_string();
        }
        let task = Task {
            id: self.next_id(),
            title: title.to_string(),
            every_secs: secs,
            next_run_at: rfc3339(now_secs().saturating_add(secs)),
            completed: false,
        };
        let id = task.id.clone();
        let next_run_at = task.next_run_at.clone();
        self.tasks.push(task);
        self.save_tasks();
        if requested_secs < MIN_TASK_INTERVAL_SECS {
            format!(
                "Added task {id}. Requested {}s was clamped to minimum {}s. Next run at {}.\r\nclaw> ",
                requested_secs,
                MIN_TASK_INTERVAL_SECS,
                next_run_at
            )
        } else {
            format!(
                "Added task {id}. Every {}s, next run at {}.\r\nclaw> ",
                secs,
                next_run_at
            )
        }
    }

    fn task_done(&mut self, prefix: &str) -> String {
        if prefix.is_empty() {
            return "Usage: /task done <id-prefix>\r\nclaw> ".to_string();
        }
        let mut found = false;
        for task in &mut self.tasks {
            if task.id.starts_with(prefix) {
                task.completed = true;
                found = true;
                break;
            }
        }
        self.save_tasks();
        if found { "Task completed.\r\nclaw> ".to_string() } else { "Task not found.\r\nclaw> ".to_string() }
    }

    fn poll(&mut self) -> String {
        let mut out = String::new();
        let now = now_secs();
        if self.config.heartbeat.enabled && now.saturating_sub(self.last_heartbeat) >= self.config.heartbeat.interval_secs {
            self.last_heartbeat = now;
            out.push_str("[heartbeat] RustMcuClaw MCU alive\r\nclaw> ");
        }

        let mut due_indexes = Vec::new();
        for (idx, task) in self.tasks.iter().enumerate() {
            if task.completed {
                continue;
            }
            if parse_rfc3339(&task.next_run_at).unwrap_or(u64::MAX) <= now {
                due_indexes.push(idx);
            }
        }
        let mut changed = false;
        for idx in due_indexes {
            let task_id = self.tasks[idx].id.clone();
            let task_title = self.tasks[idx].title.clone();
            let task_every_secs = self.tasks[idx].every_secs;
            let prompt = format_task_chat_input(&task_title);
            let answer = self.ask(&prompt);
            let _ = write!(out, "\r\n[task {}] {}\r\n{}\r\nclaw> ", task_id, task_title, answer);
            self.tasks[idx].next_run_at = rfc3339(now.saturating_add(task_every_secs));
            changed = true;
        }
        if changed {
            self.save_tasks();
        }

        // Rust-native sensor monitor loop: reads sensors, compares against
        // user-defined thresholds, and triggers actions without invoking the
        // LLM. This is the path that turns natural-language requests like
        // “等湿度高于 80 时给我发飞书�?into deterministic, low-cost loops.
        let monitor_out = self.poll_monitors(now);
        if !monitor_out.is_empty() {
            out.push_str(&monitor_out);
        }

        out
    }

    /// Iterate registered monitors, check the ones whose `next_check_at` is
    /// due, evaluate the condition against a single fresh sensor read, and
    /// fire matching actions. Returns user-visible status text (empty when
    /// nothing happened).
    fn poll_monitors(&mut self, now: u64) -> String {
        let mut out = String::new();
        let mut due_indexes: Vec<usize> = Vec::new();
        for (idx, m) in self.monitors.iter().enumerate() {
            if m.fire_once && m.fired {
                continue;
            }
            if m.next_check_at_secs <= now {
                due_indexes.push(idx);
            }
        }
        if due_indexes.is_empty() {
            return out;
        }

        let mut temp_milli_c: i32 = 0;
        let mut humidity_milli_pct: i32 = 0;
        let sensor_ok = unsafe {
            claw_mcu_read_temp_humidity(&mut temp_milli_c, &mut humidity_milli_pct) >= 0
        };

        let mut changed = false;
        for idx in due_indexes {
            // Always advance the schedule so a failing read does not pin the
            // task at “due�?and spin every poll(). `next_check_at` is
            // intentionally NOT persisted on every tick �?it is a runtime
            // schedule, not durable state. After reboot, monitors simply
            // re-schedule from `now + every_secs`. This avoids hammering the
            // SD card every `every_secs` seconds when nothing actually changed.
            {
                let m = &mut self.monitors[idx];
                m.next_check_at_secs = now.saturating_add(m.every_secs);
            }

            if !sensor_ok {
                let id = self.monitors[idx].id.clone();
                let _ = write!(out, "\r\n[monitor {}] sensor read failed\r\n", id);
                continue;
            }

            let temp_c = (temp_milli_c as f32) / 1000.0;
            let humidity_pct = (humidity_milli_pct as f32) / 1000.0;

            let (
                matched,
                value,
                action_kind,
                chat_id,
                message,
                led_name,
                led_state,
                id,
                fire_once,
            ) = {
                let m = &self.monitors[idx];
                let v = match m.field.as_str() {
                    "humidity_pct" => humidity_pct,
                    "temp_c" => temp_c,
                    _ => f32::NAN,
                };
                let hit = if v.is_nan() {
                    false
                } else {
                    match m.op.as_str() {
                        "gt" => v > m.threshold,
                        "lt" => v < m.threshold,
                        "gte" => v >= m.threshold,
                        "lte" => v <= m.threshold,
                        "eq" => (v - m.threshold).abs() < 0.001,
                        _ => false,
                    }
                };
                (
                    hit,
                    v,
                    m.action_kind.clone(),
                    m.action_chat_id.clone(),
                    m.action_message.clone(),
                    m.action_led_name.clone(),
                    m.action_led_state.clone(),
                    m.id.clone(),
                    m.fire_once,
                )
            };

            if !matched {
                continue;
            }

            let rendered = message.replace("{value}", &format!("{:.2}", value));
            let recipient = if !chat_id.is_empty() {
                chat_id
            } else if !self.active_feishu_chat_id.is_empty() {
                self.active_feishu_chat_id.clone()
            } else {
                self.config.channels.feishu.default_chat_id.clone()
            };

            match action_kind.as_str() {
                "feishu_send" => {
                    if recipient.is_empty() {
                        let _ = write!(
                            out,
                            "\r\n[monitor {}] action skipped: no chat_id (set channels.feishu.default_chat_id)\r\n",
                            id
                        );
                    } else {
                        match self.feishu_send_text(&recipient, &rendered) {
                            Ok(()) => {
                                let _ = write!(
                                    out,
                                    "\r\n[monitor {}] fired: {}\r\n",
                                    id, rendered
                                );
                                if fire_once {
                                    self.monitors[idx].fired = true;
                                    // `fired` is durable state, persist now so
                                    // a reboot does not re-trigger the same alert.
                                    changed = true;
                                }
                            }
                            Err(e) => {
                                // Leave `fired` untouched so the next tick retries.
                                let _ = write!(
                                    out,
                                    "\r\n[monitor {}] feishu send failed: {}\r\n",
                                    id, e
                                );
                            }
                        }
                    }
                }
                "led_set" => {
                    let led_idx = match self.resolve_led(&led_name) {
                        Ok(i) => i,
                        Err(e) => {
                            let _ = write!(
                                out,
                                "\r\n[monitor {}] led_set failed: {}\r\n",
                                id, e
                            );
                            continue;
                        }
                    };
                    let on = led_state == "on";
                    let ret = unsafe { claw_mcu_led_set(led_idx, if on { 1 } else { 0 }) };
                    if ret < 0 {
                        let _ = write!(
                            out,
                            "\r\n[monitor {}] led_set({}) failed: {}\r\n",
                            id, led_name, ret
                        );
                    } else {
                        let _ = write!(
                            out,
                            "\r\n[monitor {}] fired: {} -> {} (value={:.2})\r\n",
                            id, led_name, led_state, value
                        );
                        if fire_once {
                            self.monitors[idx].fired = true;
                            changed = true;
                        }
                    }
                }
                other => {
                    let _ = write!(
                        out,
                        "\r\n[monitor {}] unknown action_kind '{}'\r\n",
                        id, other
                    );
                }
            }
        }

        if changed {
            self.save_monitors();
        }
        if !out.is_empty() {
            out.push_str("claw> ");
        }
        out
    }

    fn next_id(&mut self) -> String {
        self.seq = self.seq.wrapping_add(1);
        format!("{:08x}{:08x}", now_secs() as u32, self.seq as u32)
    }

    /// Resolve a tool-supplied path:
    ///   * absolute (starts with '/') �?returned as-is
    ///   * relative �?joined under the configured data_dir
    ///   * '..' segments are rejected to prevent escaping the data_dir
    fn resolve_tool_path(&self, path: &str) -> Result<String, String> {
        let trimmed = path.trim();
        if trimmed.is_empty() {
            return Err("empty path".to_string());
        }
        for segment in trimmed.split('/') {
            if segment == ".." {
                return Err("'..' segments are not allowed".to_string());
            }
        }
        if trimmed.starts_with('/') {
            Ok(trimmed.to_string())
        } else if trimmed == "." {
            Ok(self.config.data_dir.clone())
        } else {
            Ok(join(&self.config.data_dir, trimmed))
        }
    }

    /// Dispatch a single tool call by name. Returns a short, human-readable
    /// result string that is both displayed on screen and fed back into LLM
    /// history for follow-up turns.
    ///
    /// Adding a new device (servo, temperature sensor, ...) only requires
    /// adding another arm here and documenting it in `TOOLS_PROMPT_SECTION`.
    fn execute_tool(&mut self, name: &str, args: &serde_json::Value) -> String {
        match name {
            "fs_list" => self.tool_fs_list(args),
            "fs_read" => self.tool_fs_read(args),
            "fs_write" => self.tool_fs_write(args),
            "fs_exists" => self.tool_fs_exists(args),
            "sensor_read_temp_humidity" => self.tool_sensor_read_temp_humidity(args),
            "monitor_create" => self.tool_monitor_create(args),
            "monitor_list" => self.tool_monitor_list(args),
            "monitor_delete" => self.tool_monitor_delete(args),
            "led_list" => self.tool_led_list(args),
            "led_set" => self.tool_led_set(args),
            "led_blink" => self.tool_led_blink(args),
            "strip_effect" => self.tool_strip_effect(args),
            other => format!("ERR: unknown tool '{other}'"),
        }
    }

    /// Resolve a user-facing LED name to a firmware index. Accepts the
    /// canonical labels ("led1", "led2", …) emitted by `claw_mcu_led_name`,
    /// case-insensitively. Returns Err with a friendly listing on miss so
    /// the LLM gets enough info to retry without another `led_list`.
    fn resolve_led(&self, name: &str) -> Result<i32, String> {
        let count = unsafe { claw_mcu_led_count() };
        if count <= 0 {
            return Err("ERR: no LEDs available on this board".to_string());
        }
        let want = name.trim().to_ascii_lowercase();
        let mut available: Vec<String> = Vec::new();
        for i in 0..count {
            let mut buf = [0u8; 32];
            let ret = unsafe { claw_mcu_led_name(i, buf.as_mut_ptr(), buf.len()) };
            if ret < 0 {
                continue;
            }
            let nul = buf.iter().position(|b| *b == 0).unwrap_or(buf.len());
            let label = String::from_utf8_lossy(&buf[..nul]).to_string();
            if label.eq_ignore_ascii_case(&want) {
                return Ok(i);
            }
            available.push(label);
        }
        Err(format!(
            "ERR: unknown LED '{name}'. Available: {}",
            available.join(", ")
        ))
    }

    fn tool_led_list(&self, _args: &serde_json::Value) -> String {
        let count = unsafe { claw_mcu_led_count() };
        if count <= 0 {
            return "OK: no LEDs available".to_string();
        }
        let mut out = String::from("OK: LEDs\n");
        for i in 0..count {
            let mut buf = [0u8; 32];
            let ret = unsafe { claw_mcu_led_name(i, buf.as_mut_ptr(), buf.len()) };
            if ret < 0 {
                let _ = writeln!(out, "  [{i}] <name unavailable: {ret}>");
                continue;
            }
            let nul = buf.iter().position(|b| *b == 0).unwrap_or(buf.len());
            let label = String::from_utf8_lossy(&buf[..nul]);
            let state = unsafe { claw_mcu_led_get(i) };
            let state_str = match state {
                1 => "on",
                0 => "off",
                _ => "unknown",
            };
            let _ = writeln!(out, "  {label} = {state_str}");
        }
        out
    }

    fn tool_led_set(&mut self, args: &serde_json::Value) -> String {
        let name = match args.get("name").and_then(|v| v.as_str()) {
            Some(s) => s.to_string(),
            None => return "ERR: missing string argument 'name'".to_string(),
        };
        // Accept the obvious string variants ("on"/"off", "1"/"0") and a
        // bool fallback for resilience against LLM formatting drift.
        let on = if let Some(s) = args.get("state").and_then(|v| v.as_str()) {
            match s.trim().to_ascii_lowercase().as_str() {
                "on" | "1" | "true" | "high" => true,
                "off" | "0" | "false" | "low" => false,
                other => return format!("ERR: bad state '{other}'. Use 'on' or 'off'."),
            }
        } else if let Some(b) = args.get("state").and_then(|v| v.as_bool()) {
            b
        } else if let Some(b) = args.get("on").and_then(|v| v.as_bool()) {
            b
        } else {
            return "ERR: missing argument 'state' (\"on\"|\"off\")".to_string();
        };
        let idx = match self.resolve_led(&name) {
            Ok(i) => i,
            Err(e) => return e,
        };
        let ret = unsafe { claw_mcu_led_set(idx, if on { 1 } else { 0 }) };
        if ret < 0 {
            return format!("ERR: led_set({name}) failed: {ret}");
        }
        format!("OK: {name} -> {}", if on { "on" } else { "off" })
    }

    fn tool_led_blink(&mut self, args: &serde_json::Value) -> String {
        let name = match args.get("name").and_then(|v| v.as_str()) {
            Some(s) => s.to_string(),
            None => return "ERR: missing string argument 'name'".to_string(),
        };
        let times = args.get("times").and_then(|v| v.as_u64()).unwrap_or(3);
        // Cap at 20 cycles so a stray LLM call cannot lock up `poll()` for
        // tens of seconds. The default 200 ms period also keeps the upper
        // bound modest (~8 s).
        let times = times.min(20).max(1) as u32;
        let period_ms = args
            .get("period_ms")
            .and_then(|v| v.as_u64())
            .unwrap_or(200)
            .clamp(50, 1000) as u32;
        let idx = match self.resolve_led(&name) {
            Ok(i) => i,
            Err(e) => return e,
        };
        // Remember the original state so blink is non-destructive: a blink
        // does not silently flip a status LED off.
        let prior = unsafe { claw_mcu_led_get(idx) };
        for _ in 0..times {
            unsafe {
                let _ = claw_mcu_led_set(idx, 1);
                claw_mcu_sleep_ms(period_ms);
                let _ = claw_mcu_led_set(idx, 0);
                claw_mcu_sleep_ms(period_ms);
            }
        }
        // Restore the prior state explicitly so the LED ends in a known
        // place AND so we can report it back to the caller. Without this
        // the LLM has to guess the final state and tends to hallucinate
        // (e.g. claim "灯已恢复常亮" when it is actually off).
        let final_state = if prior == 1 { "on" } else { "off" };
        unsafe {
            let _ = claw_mcu_led_set(idx, if prior == 1 { 1 } else { 0 });
        }
        format!(
            "OK: blinked {name} {times} time(s) at {period_ms}ms (final state: {final_state})"
        )
    }

    /// Control the WS2812B RGB strip on P2_1. Three persistent effects plus
    /// off; each replaces whatever was running before and keeps going until
    /// changed again (the underlying engine uses duration_ms = 0 = forever).
    ///
    /// args:
    ///   "effect": "rainbow" | "solid" | "blink" | "off"   (required)
    ///   rainbow: "speed" 1..10 (10 = fastest, default 5),
    ///            "brightness" 0..255 (default 80)
    ///   solid/blink: colour via "color" name (red/green/blue/white/yellow/
    ///                cyan/magenta/orange/purple/pink and 红/绿/蓝/白/黄/青/
    ///                紫/橙/粉) OR explicit "r","g","b" 0..255;
    ///                "brightness" 0..255 scales a named colour (default 120)
    ///   blink: extra "period_ms" 100..5000 (default 500)
    fn tool_strip_effect(&mut self, args: &serde_json::Value) -> String {
        let effect = args
            .get("effect")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim()
            .to_ascii_lowercase();

        match effect.as_str() {
            "off" | "stop" | "clear" => {
                let ret = unsafe { ws2812_strip_effect_stop() };
                if ret < 0 {
                    return format!("ERR: strip off failed: {ret}");
                }
                "OK: 灯带已关闭".to_string()
            }
            "rainbow" | "彩虹" => {
                let speed = args
                    .get("speed")
                    .and_then(|v| v.as_u64())
                    .unwrap_or(5)
                    .clamp(1, 10);
                let brightness = args
                    .get("brightness")
                    .and_then(|v| v.as_u64())
                    .unwrap_or(80)
                    .min(255) as u8;
                // speed 1 -> 120 ms/frame (slow), speed 10 -> 12 ms/frame (fast).
                let frame_ms = ((11 - speed) * 12) as u32;
                let ret = unsafe { ws2812_strip_effect_rainbow(brightness, frame_ms, 0) };
                if ret < 0 {
                    return format!("ERR: strip rainbow failed: {ret}");
                }
                format!("OK: 彩虹效果 (速度 {speed}/10, 亮度 {brightness})")
            }
            "solid" | "纯色" | "纯色灯" => {
                let (r, g, b) = match Self::parse_strip_color(args, 120) {
                    Ok(rgb) => rgb,
                    Err(e) => return e,
                };
                let ret = unsafe { ws2812_strip_effect_solid(r, g, b, 0) };
                if ret < 0 {
                    return format!("ERR: strip solid failed: {ret}");
                }
                format!("OK: 纯色灯 (R{r} G{g} B{b})")
            }
            "blink" | "闪烁" | "闪烁灯" => {
                let (r, g, b) = match Self::parse_strip_color(args, 120) {
                    Ok(rgb) => rgb,
                    Err(e) => return e,
                };
                let period_ms = args
                    .get("period_ms")
                    .and_then(|v| v.as_u64())
                    .unwrap_or(500)
                    .clamp(100, 5000) as u32;
                let ret = unsafe { ws2812_strip_effect_blink(r, g, b, period_ms, 0) };
                if ret < 0 {
                    return format!("ERR: strip blink failed: {ret}");
                }
                format!("OK: 纯色闪烁 (R{r} G{g} B{b}, 周期 {period_ms}ms)")
            }
            "" => "ERR: missing argument 'effect' (rainbow|solid|blink|off)".to_string(),
            other => format!(
                "ERR: unknown effect '{other}'. Use rainbow, solid, blink or off."
            ),
        }
    }

    /// Resolve a strip colour from `args`: explicit "r"/"g"/"b" (0..255) take
    /// priority; otherwise a "color" name is looked up and scaled by
    /// "brightness" (0..255, default `default_brightness`).
    fn parse_strip_color(
        args: &serde_json::Value,
        default_brightness: u8,
    ) -> Result<(u8, u8, u8), String> {
        let has_rgb = args.get("r").is_some()
            || args.get("g").is_some()
            || args.get("b").is_some();
        if has_rgb {
            let r = args.get("r").and_then(|v| v.as_u64()).unwrap_or(0).min(255) as u8;
            let g = args.get("g").and_then(|v| v.as_u64()).unwrap_or(0).min(255) as u8;
            let b = args.get("b").and_then(|v| v.as_u64()).unwrap_or(0).min(255) as u8;
            return Ok((r, g, b));
        }

        let name = args
            .get("color")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim()
            .to_ascii_lowercase();
        if name.is_empty() {
            return Err(
                "ERR: missing colour: provide 'color' name or 'r'/'g'/'b' (0..255)".to_string(),
            );
        }
        // Base colour at full scale; brightness scales it down to protect the
        // 5 V rail and let the user dim a named colour.
        let base: (u32, u32, u32) = match name.as_str() {
            "red" | "红" | "红色" => (255, 0, 0),
            "green" | "绿" | "绿色" => (0, 255, 0),
            "blue" | "蓝" | "蓝色" => (0, 0, 255),
            "white" | "白" | "白色" => (255, 255, 255),
            "yellow" | "黄" | "黄色" => (255, 255, 0),
            "cyan" | "青" | "青色" => (0, 255, 255),
            "magenta" | "品红" => (255, 0, 255),
            "purple" | "violet" | "紫" | "紫色" => (160, 32, 240),
            "orange" | "橙" | "橙色" => (255, 110, 0),
            "pink" | "粉" | "粉色" | "粉红" => (255, 96, 160),
            other => {
                return Err(format!(
                    "ERR: unknown colour '{other}'. Use red/green/blue/white/yellow/cyan/magenta/purple/orange/pink or r/g/b."
                ))
            }
        };
        let brightness = args
            .get("brightness")
            .and_then(|v| v.as_u64())
            .unwrap_or(default_brightness as u64)
            .min(255);
        let scale = |c: u32| ((c * brightness as u32) / 255) as u8;
        Ok((scale(base.0), scale(base.1), scale(base.2)))
    }

    fn tool_fs_list(&self, args: &serde_json::Value) -> String {
        let path = match args.get("path").and_then(|v| v.as_str()) {
            Some(p) => p,
            None => return "ERR: missing string argument 'path'".to_string(),
        };
        let resolved = match self.resolve_tool_path(path) {
            Ok(p) => p,
            Err(e) => return format!("ERR: {e}"),
        };
        let c_path = c_string(&resolved);
        let mut buf = vec_with_len(8 * 1024);
        let ret = unsafe {
            claw_mcu_fs_list(c_path.as_ptr() as *const c_char, buf.as_mut_ptr(), buf.len())
        };
        if ret < 0 {
            return format!("ERR: fs_list({resolved}) failed: {ret}");
        }
        buf.truncate(ret as usize);
        let text = String::from_utf8_lossy(&buf).into_owned();
        if text.trim().is_empty() {
            format!("OK: '{resolved}' is empty")
        } else {
            format!("OK: listing of '{resolved}'\n{}", text.trim_end())
        }
    }

    fn tool_fs_read(&self, args: &serde_json::Value) -> String {
        let path = match args.get("path").and_then(|v| v.as_str()) {
            Some(p) => p,
            None => return "ERR: missing string argument 'path'".to_string(),
        };
        let resolved = match self.resolve_tool_path(path) {
            Ok(p) => p,
            Err(e) => return format!("ERR: {e}"),
        };
        // Cap at 8 KiB to keep PSRAM usage and LLM context bounded.
        let c_path = c_string(&resolved);
        let mut buf = vec_with_len(8 * 1024);
        let ret = unsafe {
            claw_mcu_fs_read(c_path.as_ptr() as *const c_char, buf.as_mut_ptr(), buf.len())
        };
        if ret < 0 {
            return format!("ERR: fs_read({resolved}) failed: {ret}");
        }
        buf.truncate(ret as usize);
        let text = String::from_utf8_lossy(&buf).into_owned();
        format!("OK: contents of '{resolved}' ({} bytes)\n{}", text.len(), text)
    }

    fn tool_fs_write(&self, args: &serde_json::Value) -> String {
        let path = match args.get("path").and_then(|v| v.as_str()) {
            Some(p) => p,
            None => return "ERR: missing string argument 'path'".to_string(),
        };
        let content = match args.get("content").and_then(|v| v.as_str()) {
            Some(c) => c,
            None => return "ERR: missing string argument 'content'".to_string(),
        };
        let append = args.get("append").and_then(|v| v.as_bool()).unwrap_or(false);
        let resolved = match self.resolve_tool_path(path) {
            Ok(p) => p,
            Err(e) => return format!("ERR: {e}"),
        };
        let c_path = c_string(&resolved);
        let ret = unsafe {
            claw_mcu_fs_write(
                c_path.as_ptr() as *const c_char,
                content.as_ptr(),
                content.len(),
                append,
            )
        };
        if ret < 0 {
            return format!("ERR: fs_write({resolved}) failed: {ret}");
        }
        let mode = if append { "appended" } else { "wrote" };
        format!("OK: {mode} {} bytes to '{resolved}'", content.len())
    }

    fn tool_fs_exists(&self, args: &serde_json::Value) -> String {
        let path = match args.get("path").and_then(|v| v.as_str()) {
            Some(p) => p,
            None => return "ERR: missing string argument 'path'".to_string(),
        };
        let resolved = match self.resolve_tool_path(path) {
            Ok(p) => p,
            Err(e) => return format!("ERR: {e}"),
        };
        let c_path = c_string(&resolved);
        let exists = unsafe { claw_mcu_fs_exists(c_path.as_ptr() as *const c_char) > 0 };
        format!("OK: '{resolved}' {}", if exists { "exists" } else { "does not exist" })
    }

    fn tool_sensor_read_temp_humidity(&self, _args: &serde_json::Value) -> String {
        let mut temp_milli_c = 0i32;
        let mut humidity_milli_pct = 0i32;
        let ret = unsafe { claw_mcu_read_temp_humidity(&mut temp_milli_c, &mut humidity_milli_pct) };
        if ret < 0 {
            return format!("ERR: sensor_read_temp_humidity failed: {ret}");
        }

        format!(
            "OK: ambient temperature {} C, humidity {} %RH",
            format_milli_value(temp_milli_c),
            format_milli_value(humidity_milli_pct)
        )
    }

    fn tool_monitor_create(&mut self, args: &serde_json::Value) -> String {
        let field = match args.get("field").and_then(|v| v.as_str()) {
            Some(s) => s.to_string(),
            None => return "ERR: missing string argument 'field'".to_string(),
        };
        if !matches!(field.as_str(), "humidity_pct" | "temp_c") {
            return format!(
                "ERR: unknown field '{}'. Allowed: humidity_pct, temp_c",
                field
            );
        }
        let op = match args.get("op").and_then(|v| v.as_str()) {
            Some(s) => s.to_string(),
            None => return "ERR: missing string argument 'op'".to_string(),
        };
        if !matches!(op.as_str(), "gt" | "lt" | "gte" | "lte" | "eq") {
            return format!("ERR: unknown op '{}'. Allowed: gt, lt, gte, lte, eq", op);
        }
        let threshold = match args.get("threshold").and_then(|v| v.as_f64()) {
            Some(n) => n as f32,
            None => return "ERR: missing number argument 'threshold'".to_string(),
        };
        let action = args
            .get("action")
            .and_then(|v| v.as_str())
            .unwrap_or("feishu_send")
            .to_string();
        if !matches!(action.as_str(), "feishu_send" | "led_set") {
            return format!(
                "ERR: unknown action '{}'. Allowed: feishu_send, led_set",
                action
            );
        }
        let requested_secs = args
            .get("every_secs")
            .and_then(|v| v.as_u64())
            .unwrap_or(30);
        let every_secs = requested_secs.max(MIN_MONITOR_INTERVAL_SECS);
        let fire_once = args
            .get("fire_once")
            .and_then(|v| v.as_bool())
            .unwrap_or(true);

        // Per-action argument parsing. Each branch fills in the durable
        // fields that `poll_monitors` will read back at fire time.
        let (chat_id, message, led_name, led_state) = match action.as_str() {
            "feishu_send" => {
                let chat_id = args
                    .get("chat_id")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let message = match args.get("message").and_then(|v| v.as_str()) {
                    Some(s) => s.to_string(),
                    None => return "ERR: missing string argument 'message'".to_string(),
                };
                // Recipient priority: explicit chat_id > active Feishu session
                // > configured default_chat_id. This means that if you tell
                // Claw via Feishu, it already knows where to send the alert.
                let effective_chat = if !chat_id.is_empty() {
                    chat_id.clone()
                } else if !self.active_feishu_chat_id.is_empty() {
                    self.active_feishu_chat_id.clone()
                } else {
                    self.config.channels.feishu.default_chat_id.clone()
                };
                if effective_chat.is_empty() {
                    return "ERR: no Feishu chat_id available. Either send this request from a Feishu chat so Claw can auto-detect it, or fill in channels.feishu.default_chat_id in rmcc.toml".to_string();
                }
                (chat_id, message, String::new(), String::new())
            }
            "led_set" => {
                let led_name = match args.get("led").and_then(|v| v.as_str()) {
                    Some(s) => s.to_string(),
                    None => {
                        return "ERR: missing string argument 'led' (e.g. \"led1\")"
                            .to_string()
                    }
                };
                // Validate the LED exists right now so the user gets the
                // error at create time instead of silently at fire time.
                if let Err(e) = self.resolve_led(&led_name) {
                    return e;
                }
                let state = args
                    .get("state")
                    .and_then(|v| v.as_str())
                    .unwrap_or("on")
                    .trim()
                    .to_ascii_lowercase();
                if !matches!(state.as_str(), "on" | "off") {
                    return format!(
                        "ERR: bad state '{state}'. Use 'on' or 'off'."
                    );
                }
                (String::new(), String::new(), led_name, state)
            }
            _ => unreachable!(),
        };

        let now = now_secs();
        let id = self.next_id();
        let next_check_at_secs = now.saturating_add(every_secs);
        let monitor = Monitor {
            id: id.clone(),
            every_secs,
            next_check_at_secs,
            field: field.clone(),
            op: op.clone(),
            threshold,
            action_kind: action.clone(),
            action_chat_id: chat_id,
            action_message: message,
            action_led_name: led_name,
            action_led_state: led_state,
            fire_once,
            fired: false,
        };
        self.monitors.push(monitor);
        self.save_monitors();
        format!(
            "OK: monitor {id} created on {field} {op} {threshold}, every {every_secs}s, fire_once={fire_once}"
        )
    }

    fn tool_monitor_list(&self, _args: &serde_json::Value) -> String {
        if self.monitors.is_empty() {
            return "OK: no monitors".to_string();
        }
        let mut out = String::from("OK: monitors\n");
        for m in &self.monitors {
            let state = if m.fired { "fired" } else { "active" };
            let _ = writeln!(
                out,
                "  {} [{}] {} {} {} every {}s next_in ~{}s action={} chat_id={} fire_once={}",
                m.id,
                state,
                m.field,
                m.op,
                m.threshold,
                m.every_secs,
                m.next_check_at_secs.saturating_sub(now_secs()),
                m.action_kind,
                if m.action_chat_id.is_empty() {
                    "<default>"
                } else {
                    m.action_chat_id.as_str()
                },
                m.fire_once,
            );
        }
        out
    }

    fn tool_monitor_delete(&mut self, args: &serde_json::Value) -> String {
        let id_prefix = match args.get("id").and_then(|v| v.as_str()) {
            Some(s) => s.to_string(),
            None => return "ERR: missing string argument 'id'".to_string(),
        };
        if id_prefix.is_empty() {
            return "ERR: empty id prefix".to_string();
        }
        let before = self.monitors.len();
        self.monitors.retain(|m| !m.id.starts_with(&id_prefix));
        let removed = before - self.monitors.len();
        self.save_monitors();
        format!("OK: removed {removed} monitor(s) matching id prefix '{id_prefix}'")
    }
}

/// Render an app secret as `head…tail` so /doctor never leaks the full value
/// to a shared console while still letting the operator confirm the right
/// credential is loaded.
fn mask_secret(value: &str) -> String {
    if value.is_empty() {
        return "<unset>".to_string();
    }
    let chars: Vec<char> = value.chars().collect();
    if chars.len() <= 4 {
        return "****".to_string();
    }
    let head: String = chars.iter().take(2).collect();
    let tail: String = chars.iter().rev().take(2).collect::<String>().chars().rev().collect();
    format!("{head}…{tail}")
}

/// Escape a UTF-8 string so it can be embedded inside a JSON string literal.
/// Only the characters JSON requires escaped are touched; non-ASCII bytes are
/// passed through verbatim because Feishu's API accepts raw UTF-8.
fn json_escape(value: &str) -> String {
    let mut out = String::with_capacity(value.len() + 8);
    for c in value.chars() {
        match c {
            '"'  => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\u{08}' => out.push_str("\\b"),
            '\u{0c}' => out.push_str("\\f"),
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out
}

/// Feishu group messages prefix the bot with `@_user_1` style mentions that
/// the user did not actually type. Strip them so the LLM sees only intent.
fn strip_feishu_mention(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '@' {
            // Drop "@token" up to the next whitespace, then consume one
            // separating space if present.
            while let Some(&nc) = chars.peek() {
                if nc.is_whitespace() {
                    break;
                }
                chars.next();
            }
            if let Some(&nc) = chars.peek() {
                if nc == ' ' {
                    chars.next();
                }
            }
            continue;
        }
        out.push(c);
    }
    out.trim().to_string()
}

fn format_milli_value(value: i32) -> String {
    let sign = if value < 0 { "-" } else { "" };
    let abs_value = i64::from(value).abs();
    let whole = abs_value / 1000;
    let frac = abs_value % 1000;
    format!("{}{}.{:03}", sign, whole, frac)
}

/// A single tool-call request emitted by the LLM inside the assistant message.
#[derive(Clone)]
struct ToolCall {
    /// The full `<tool_call>...</tool_call>` span in the raw assistant text;
    /// used to strip the call from the user-visible reply.
    span: (usize, usize),
    name: String,
    arguments: serde_json::Value,
}

/// Locate every `<tool_call>...</tool_call>` block in the raw LLM reply and
/// parse the inner JSON. Malformed or unknown-shape blocks are skipped
/// silently �?the calling logic surfaces a tool error string in their place.
fn parse_tool_calls(raw: &str) -> Vec<ToolCall> {
    const OPEN: &str = "<tool_call>";
    const CLOSE: &str = "</tool_call>";
    let bytes = raw.as_bytes();
    let mut out = Vec::new();
    let mut cursor = 0usize;
    while cursor < bytes.len() {
        let Some(open_rel) = raw[cursor..].find(OPEN) else { break };
        let open_abs = cursor + open_rel;
        let inner_start = open_abs + OPEN.len();
        let Some(close_rel) = raw[inner_start..].find(CLOSE) else { break };
        let close_abs = inner_start + close_rel;
        let span_end = close_abs + CLOSE.len();

        let payload = raw[inner_start..close_abs].trim();
        if let Ok(value) = serde_json::from_str::<serde_json::Value>(payload) {
            if let Some(name) = value.get("name").and_then(|v| v.as_str()) {
                let arguments = value
                    .get("arguments")
                    .cloned()
                    .unwrap_or_else(|| serde_json::Value::Object(serde_json::Map::new()));
                out.push(ToolCall {
                    span: (open_abs, span_end),
                    name: name.to_string(),
                    arguments,
                });
            }
        }
        cursor = span_end;
    }
    out
}

/// Replace each parsed `<tool_call>` span with a compact `[�?name]` marker so
/// the displayed assistant reply does not contain raw JSON.
fn strip_tool_calls(raw: &str, calls: &[ToolCall]) -> String {
    if calls.is_empty() {
        return raw.to_string();
    }
    let mut out = String::with_capacity(raw.len());
    let mut last = 0usize;
    for call in calls {
        let (start, end) = call.span;
        if start >= last {
            out.push_str(&raw[last..start]);
            let _ = write!(out, "[�?{}]", call.name);
            last = end;
        }
    }
    if last < raw.len() {
        out.push_str(&raw[last..]);
    }
    out
}

fn parse_config(text: &str, mut cfg: Config) -> Config {
    let mut section = "";
    for raw in text.lines() {
        let line = raw.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') {
            section = &line[1..line.len() - 1];
            continue;
        }
        let Some((key, value)) = line.split_once('=') else { continue; };
        let key = key.trim();
        let value = value.trim();
        match (section, key) {
            ("", "data_dir") => cfg.data_dir = parse_string(value),
            ("provider", "kind") => cfg.provider.kind = parse_string(value),
            ("provider", "model") => cfg.provider.model = parse_string(value),
            ("provider", "endpoint") => cfg.provider.endpoint = parse_string(value),
            ("provider", "api_key") => cfg.provider.api_key = parse_string(value),
            ("provider", "temperature") => cfg.provider.temperature = value.parse().unwrap_or(cfg.provider.temperature),
            ("provider", "max_tokens") => cfg.provider.max_tokens = value.parse().unwrap_or(cfg.provider.max_tokens),
            ("chat", "history_limit") => cfg.chat.history_limit = value.parse().unwrap_or(cfg.chat.history_limit),
            ("chat", "max_tool_loops") => cfg.chat.max_tool_loops = value.parse().unwrap_or(cfg.chat.max_tool_loops),
            ("heartbeat", "enabled") => cfg.heartbeat.enabled = parse_bool(value),
            ("heartbeat", "interval_secs") => cfg.heartbeat.interval_secs = value.parse().unwrap_or(cfg.heartbeat.interval_secs),
            ("files", "soul") => cfg.files.soul = parse_string(value),
            ("files", "user") => cfg.files.user = parse_string(value),
            ("files", "role") => cfg.files.role = parse_string(value),
            ("files", "summary_memory") => cfg.files.summary_memory = parse_string(value),
            ("files", "tasks") => cfg.files.tasks = parse_string(value),
            ("files", "monitors") => cfg.files.monitors = parse_string(value),
            ("channels.feishu", "enabled") => cfg.channels.feishu.enabled = parse_bool(value),
            ("channels.feishu", "app_id") => cfg.channels.feishu.app_id = parse_string(value),
            ("channels.feishu", "app_secret") => cfg.channels.feishu.app_secret = parse_string(value),
            ("channels.feishu", "endpoint") => cfg.channels.feishu.endpoint = parse_string(value),
            ("channels.feishu", "default_chat_id") => cfg.channels.feishu.default_chat_id = parse_string(value),
            _ => {}
        }
    }
    cfg
}

fn parse_string(value: &str) -> String {
    let value = value.trim();
    if value.starts_with('"') && value.ends_with('"') && value.len() >= 2 {
        value[1..value.len() - 1].to_string()
    } else {
        value.to_string()
    }
}

fn parse_bool(value: &str) -> bool {
    matches!(value.trim(), "true" | "1" | "yes" | "on")
}

fn ensure_file(path: &str, default: &str) -> Result<(), String> {
    if path_exists(path) {
        Ok(())
    } else {
        write_text(path, default, false)
    }
}

fn load_tasks_from_path(path: &str) -> Vec<Task> {
    let text = read_text(path).unwrap_or_else(|_| "[]".to_string());
    serde_json::from_str::<Vec<Task>>(&text).unwrap_or_default()
}

fn load_monitors_from_path(path: &str) -> Vec<Monitor> {
    let text = read_text(path).unwrap_or_else(|_| "[]".to_string());
    let mut monitors = serde_json::from_str::<Vec<Monitor>>(&text).unwrap_or_default();
    // Reschedule all non-fired monitors from now so the device does not try
    // to make up for missed checks after a reboot.
    let now = now_secs();
    for m in &mut monitors {
        m.next_check_at_secs = now.saturating_add(m.every_secs);
    }
    monitors
}

fn path_exists(path: &str) -> bool {
    let c_path = c_string(path);
    unsafe { claw_mcu_fs_exists(c_path.as_ptr() as *const c_char) > 0 }
}

fn read_text(path: &str) -> Result<String, String> {
    let c_path = c_string(path);
    let mut buf = vec_with_len(64 * 1024);
    let ret = unsafe { claw_mcu_fs_read(c_path.as_ptr() as *const c_char, buf.as_mut_ptr(), buf.len()) };
    if ret < 0 {
        return Err(format!("read {path} failed: {ret}"));
    }
    buf.truncate(ret as usize);
    Ok(String::from_utf8_lossy(&buf).into_owned())
}

fn write_text(path: &str, text: &str, append: bool) -> Result<(), String> {
    let c_path = c_string(path);
    let ret = unsafe { claw_mcu_fs_write(c_path.as_ptr() as *const c_char, text.as_ptr(), text.len(), append) };
    if ret < 0 {
        Err(format!("write {path} failed: {ret}"))
    } else {
        Ok(())
    }
}

fn join(dir: &str, name: &str) -> String {
    if name.starts_with('/') || name.contains(':') {
        name.to_string()
    } else if dir.ends_with('/') {
        format!("{dir}{name}")
    } else {
        format!("{dir}/{name}")
    }
}

fn c_string(s: &str) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len() + 1);
    out.extend(s.bytes().filter(|b| *b != 0));
    out.push(0);
    out
}

unsafe fn cstr_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    Some(CStr::from_ptr(ptr).to_string_lossy().into_owned())
}

fn write_out(out: *mut u8, out_cap: usize, text: &str) {
    if out.is_null() || out_cap == 0 {
        return;
    }
    let bytes = text.as_bytes();
    let len = min(bytes.len(), out_cap.saturating_sub(1));
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), out, len);
        *out.add(len) = 0;
    }
}

fn vec_with_len(len: usize) -> Vec<u8> {
    let mut v = Vec::with_capacity(len);
    unsafe { v.set_len(len); }
    v
}

fn now_secs() -> u64 {
    unsafe { claw_mcu_now_secs() }
}

fn rfc3339(secs: u64) -> String {
    let days = (secs / 86_400) as i64;
    let rem = secs % 86_400;
    let (year, month, day) = civil_from_days(days);
    let hour = rem / 3600;
    let minute = (rem % 3600) / 60;
    let second = rem % 60;
    format!("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", year, month, day, hour, minute, second)
}

fn parse_rfc3339(s: &str) -> Option<u64> {
    if s.len() < 19 {
        return None;
    }
    let year = s.get(0..4)?.parse::<i32>().ok()?;
    let month = s.get(5..7)?.parse::<u32>().ok()?;
    let day = s.get(8..10)?.parse::<u32>().ok()?;
    let hour = s.get(11..13)?.parse::<u64>().ok()?;
    let minute = s.get(14..16)?.parse::<u64>().ok()?;
    let second = s.get(17..19)?.parse::<u64>().ok()?;
    let days = days_from_civil(year, month, day)?;
    Some((days as u64) * 86_400 + hour * 3600 + minute * 60 + second)
}

fn civil_from_days(days_since_epoch: i64) -> (i32, u32, u32) {
    let z = days_since_epoch + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = mp + if mp < 10 { 3 } else { -9 };
    let year = y + if m <= 2 { 1 } else { 0 };
    (year as i32, m as u32, d as u32)
}

fn days_from_civil(year: i32, month: u32, day: u32) -> Option<i64> {
    if !(1..=12).contains(&month) || !(1..=31).contains(&day) {
        return None;
    }
    let y = year as i64 - if month <= 2 { 1 } else { 0 };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let m = month as i64;
    let doy = (153 * (m + if m > 2 { -3 } else { 9 }) + 2) / 5 + day as i64 - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    Some(era * 146_097 + doe - 719_468)
}
