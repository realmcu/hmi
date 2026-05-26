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
    DEFAULT_MAX_TOOL_LOOPS, HISTORY_CONTENT_MAX, MIN_TASK_INTERVAL_SECS,
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

struct App {
    config: Config,
    history: Vec<Message>,
    soul: String,
    user: String,
    role: String,
    summary_memory: String,
    tasks: Vec<Task>,
    seq: u64,
    last_heartbeat: u64,
    /// Cached Feishu tenant_access_token. Empty when never fetched or expired.
    feishu_token: String,
    /// Unix epoch (seconds) at which `feishu_token` stops being valid.
    feishu_token_expires_at: u64,
    /// Small ring of recently-handled Feishu `message_id`s for deduplication.
    feishu_recent_ids: Vec<String>,
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
    "tasks = \"tasks.json\"\n\n",
    "[channels.feishu]\n",
    "# Feishu / Lark open-platform app. Long-connection event subscription is WIP.\n",
    "# App Secret is sensitive — write the real value into the SD card copy of\n",
    "# rmcc.toml manually; the firmware never embeds it.\n",
    "enabled = false\n",
    "app_id = \"cli_a92512f2f3391bd4\"\n",
    "app_secret = \"\"\n",
    "endpoint = \"https://open.feishu.cn\"\n",
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

    let soul_path = join(&config.data_dir, &config.files.soul);
    let user_path = join(&config.data_dir, &config.files.user);
    let role_path = join(&config.data_dir, &config.files.role);
    let summary_path = join(&config.data_dir, &config.files.summary_memory);
    let tasks_path = join(&config.data_dir, &config.files.tasks);

    APP = Some(App {
        soul: read_text(&soul_path).unwrap_or_else(|_| DEFAULT_SOUL.to_string()),
        user: read_text(&user_path).unwrap_or_else(|_| DEFAULT_USER.to_string()),
        role: read_text(&role_path).unwrap_or_else(|_| DEFAULT_ROLE.to_string()),
        summary_memory: read_text(&summary_path).unwrap_or_default(),
        tasks: load_tasks_from_path(&tasks_path),
        config,
        history: Vec::new(),
        seq: 0,
        last_heartbeat: now_secs(),
        feishu_token: String::new(),
        feishu_token_expires_at: 0,
        feishu_recent_ids: Vec::new(),
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
/// * `pixels`  – pointer to the RGB565 framebuffer (row-major, `width × height` `u16` words).
/// * `width`   – framebuffer width in pixels.
/// * `height`  – framebuffer height in pixels.
/// * `x`, `y` – top-left text origin (may be 0).
/// * `text`    – null-terminated UTF-8 string to render.
/// * `fg`      – foreground colour in RGB565.
/// * `scale`   – pixel magnification: 1 → 8×8 per glyph, 2 → 16×16, etc.
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
            // Non-ASCII glyph not in PSRAM font — advance one halfwidth cell.
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
        format!(
            "RustMcuClaw MCU doctor:\r\n  data_dir: {}\r\n  provider: {} / {}\r\n  endpoint: {}\r\n  history_limit: {}\r\n  max_tool_loops: {}\r\n  heartbeat: {} / {}s\r\n  heap: PSRAM bump allocator\r\n  fs: PSRAM FATFS via Zephyr FS callbacks\r\n  net: Z2Plus HTTPS callback\r\n  channel.feishu: {} app_id={} secret={} endpoint={}\r\nclaw> ",
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
        Ok(())
    }

    fn save_tasks(&self) {
        if let Ok(mut text) = serde_json::to_string_pretty(&self.tasks) {
            text.push('\n');
            let _ = write_text(&join(&self.config.data_dir, &self.config.files.tasks), &text, false);
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
        out
    }

    fn next_id(&mut self) -> String {
        self.seq = self.seq.wrapping_add(1);
        format!("{:08x}{:08x}", now_secs() as u32, self.seq as u32)
    }

    /// Resolve a tool-supplied path:
    ///   * absolute (starts with '/') → returned as-is
    ///   * relative → joined under the configured data_dir
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
    fn execute_tool(&self, name: &str, args: &serde_json::Value) -> String {
        match name {
            "fs_list" => self.tool_fs_list(args),
            "fs_read" => self.tool_fs_read(args),
            "fs_write" => self.tool_fs_write(args),
            "fs_exists" => self.tool_fs_exists(args),
            "sensor_read_temp_humidity" => self.tool_sensor_read_temp_humidity(args),
            other => format!("ERR: unknown tool '{other}'"),
        }
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
/// silently — the calling logic surfaces a tool error string in their place.
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

/// Replace each parsed `<tool_call>` span with a compact `[→ name]` marker so
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
            let _ = write!(out, "[→ {}]", call.name);
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
            ("channels.feishu", "enabled") => cfg.channels.feishu.enabled = parse_bool(value),
            ("channels.feishu", "app_id") => cfg.channels.feishu.app_id = parse_string(value),
            ("channels.feishu", "app_secret") => cfg.channels.feishu.app_secret = parse_string(value),
            ("channels.feishu", "endpoint") => cfg.channels.feishu.endpoint = parse_string(value),
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
