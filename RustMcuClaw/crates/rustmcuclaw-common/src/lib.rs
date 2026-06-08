#![no_std]

extern crate alloc;

use alloc::format;
use alloc::string::String;

pub const DEFAULT_MAX_TOKENS: u32 = 2048;
pub const DEFAULT_HISTORY_LIMIT: usize = 6;
pub const DEFAULT_HEARTBEAT_INTERVAL_SECS: u64 = 60;
/// Maximum number of "LLM → tool → result → LLM" cycles allowed for a single
/// user message. Keeps cost/latency bounded if the model insists on chaining
/// tool calls forever. 1 means classic single-shot (no follow-up).
pub const DEFAULT_MAX_TOOL_LOOPS: u32 = 4;

pub const CHAT_REQUEST_BODY_MAX: usize = 64 * 1024;
pub const SYSTEM_PROMPT_MAX: usize = 64 * 1024;
pub const HISTORY_CONTENT_MAX: usize = 64 * 1024;
pub const USER_INPUT_MAX: usize = 64 * 1024;

pub const MIN_TASK_INTERVAL_SECS: u64 = 30;
/// Minimum interval for Rust-native sensor monitors. These never invoke the
/// LLM, so the limit is purely about sensor read frequency and battery/network
/// friendliness — much shorter than the LLM task minimum is fine.
pub const MIN_MONITOR_INTERVAL_SECS: u64 = 1;
pub const TASK_EXECUTION_PROMPT_PREFIX: &str = "【定时任务已到时间】不要把这句话理解为创建提醒、设置任务或记录待办。现在请直接执行这条任务，并面向用户给出回复。任务内容：";
pub const NO_LONG_TERM_MEMORY_TEXT: &str = "本次对话无长期记忆";
pub const SUMMARY_MEMORY_EMPTY_TEXT: &str = "暂无历史总结记忆。";
pub const EMPTY_SOUL_PLACEHOLDER: &str = "暂无灵魂设定。";
pub const EMPTY_USER_PLACEHOLDER: &str = "暂无用户描述。";
pub const EMPTY_ROLE_PLACEHOLDER: &str = "暂无角色约束。";
pub const SUMMARY_COMPRESSION_SYSTEM_PROMPT: &str = "你是对话记忆压缩器。请把完整聊天总结成一条可长期保存的记忆。要求：1. 使用简体中文；2. 不超过50个字或等价字符；3. 只保留用户长期偏好、约定、目标、身份事实或后续仍有价值的信息；4. 不要输出解释、前缀、序号、引号；5. 如果没有值得保留的长期信息，只输出：本次对话无长期记忆。";
pub const PC_RUNTIME_DESCRIPTION: &str = "你正在扮演一个有总结记忆和定时任务能力的 PC 终端助手。";
pub const MCU_RUNTIME_DESCRIPTION: &str = "运行环境：RTL8783G + Zephyr，多线程串口 CLI，PSRAM FATFS JSON/TOML/Markdown 文件，Z2Plus HTTPS 云 LLM。";

/// 工具调用说明（中文）。包含可用工具列表、调用语法和示例。
/// 在系统提示词末尾追加，让 LLM 知道何时以及如何发起函数调用。
/// 设计目标：
///   1. 协议与后续工具（舵机、温度传感器、IMU 等）共享，只需在工具列表里追加条目即可；
///   2. 调用语法是单行 JSON，外面用 `<tool_call>...</tool_call>` 标签包裹，便于 claw 解析；
///   3. 当用户的请求不需要工具时，LLM 直接用自然语言回答即可，不要发起调用。
pub const TOOLS_PROMPT_SECTION: &str = concat!(
    "\n\n[Tools]\n",
    "你（claw）可以调用以下设备工具完成自然语言下达的任务，包括 SD 卡文件操作和环境传感器读取：\n",
    "1. fs_list  参数 {\"path\": \"目录路径\"}                       ── 列出目录下的条目（每行: 名称<TAB>类型<TAB>字节大小）。\n",
    "2. fs_read  参数 {\"path\": \"文件路径\"}                       ── 读取文本文件内容（过大时会截断）。\n",
    "3. fs_write 参数 {\"path\": \"文件路径\", \"content\": \"...\", \"append\": false} ── 写入/覆盖文件；append=true 时追加。\n",
    "4. fs_exists 参数 {\"path\": \"文件路径\"}                      ── 判断文件或目录是否存在。\n",
    "5. sensor_read_temp_humidity 参数 {}                                ── 读取当前温湿度传感器，返回摄氏温度和相对湿度。\n",
    "6. monitor_create 参数 {\"field\":\"humidity_pct|temp_c\",\"op\":\"gt|lt|gte|lte|eq\",\"threshold\":80,\"action\":\"feishu_send|led_set\",...,\"every_secs\":5,\"fire_once\":true} ── 创建一条由 Rust 原生轮询的传感器告警规则：每 every_secs 秒读一次传感器，命中条件时由 Rust 直接调用动作，不再回到 LLM。fire_once=true 表示触发一次后停用。\n",
    "    · action=\"feishu_send\" 时还需 \"message\":\"提示文本，可含 {value}（{value} 会被替换为实际读数，保留两位小数）\"，可选 \"chat_id\"（不填默认走当前 Feishu 会话 / channels.feishu.default_chat_id）。\n",
    "    · action=\"led_set\" 时还需 \"led\":\"led1|...\" 与 \"state\":\"on|off\"，由 Rust 直接驱动板载 LED，不再调 LLM、不再走网络。先用 led_list 看可用灯名。\n",
    "7. monitor_list 参数 {}                                              ── 列出全部告警规则及其状态。\n",
    "8. monitor_delete 参数 {\"id\":\"规则 id 前缀\"}                       ── 按 id 前缀删除告警规则。\n",
    "9. led_list   参数 {}                                                ── 列出板载直连 GPIO LED（标签 + 当前状态）。把灯名映射到具体 PIN 是固件的事，LLM 只用名字。\n",
    "10. led_set   参数 {\"name\":\"led1|...\",\"state\":\"on|off\"}             ── 立即点亮/熄灭某盏灯。\n",
    "11. led_blink 参数 {\"name\":\"led1|...\",\"times\":3,\"period_ms\":200}    ── 闪烁 N 次（times ≤ 20，period_ms ∈ [50,1000]）。结束后恢复原状态。\n",
    "12. strip_effect 参数 {\"effect\":\"rainbow|solid|blink|off\", ...} ── 控制 P2_1 上的 WS2812B RGB 灯带，效果会一直保持直到再次更改：\n",
    "    · effect=\"rainbow\"：彩虹流动，可选 \"speed\":1..10（10 最快，默认 5）、\"brightness\":0..255（默认 80）。\n",
    "    · effect=\"solid\"：纯色常亮，用 \"color\":\"red|green|blue|white|yellow|cyan|magenta|purple|orange|pink 或中文红/绿/蓝/白/黄/青/紫/橙/粉\" 指定颜色，或直接给 \"r\"/\"g\"/\"b\":0..255；命名颜色可用 \"brightness\":0..255 调暗。\n",
    "    · effect=\"blink\"：纯色闪烁，颜色参数同 solid，另加 \"period_ms\":100..5000（默认 500）。\n",
    "    · effect=\"off\"：关闭灯带。\n",
    "\n",
    "调用协议：\n",
    "- 当且仅当用户的请求需要操作 SD 卡或后续会扩展的设备时，回复正文末尾追加一行：\n",
    "  <tool_call>{\"name\":\"工具名\",\"arguments\":{...}}</tool_call>\n",
    "- 一次回复可以追加多行 <tool_call>，按顺序执行；不需要工具时不要写 <tool_call>。\n",
    "- arguments 必须是合法 JSON 对象；字符串中如含换行请用 \\n 转义。\n",
    "- 默认工作目录是 SD 卡 `/SD:/RustMcuClaw/`；相对路径会自动解析到该目录下。绝对路径必须以 `/SD:/` 开头。\n",
    "\n",
    "多轮工具循环：\n",
    "- claw 会自动执行 <tool_call> 并把结果作为 user 消息（以 `[tool_results]` 为标记）回灌给你，循环最多 N 轮（由 `chat.max_tool_loops` 决定）。\n",
    "- 拿到工具结果后，请基于结果决定：(a) 直接面向用户给出最终回答，或 (b) 再追加新的 <tool_call> 继续探索（例如先 fs_list 找文件、再 fs_read 看内容）。\n",
    "- 当你已经收集到足够信息时，请用自然语言总结回复用户，并且不要再发出 <tool_call>，否则循环会被强制截断。\n",
    "- 工具结果只在你被回灌之后才存在，不要凭空假设文件已读到。\n",
    "\n",
    "示例：\n",
    "用户：帮我找找卡里的小说文件。\n",
    "助手（第 1 轮）：好的，我先扫描数据目录。<tool_call>{\"name\":\"fs_list\",\"arguments\":{\"path\":\".\"}}</tool_call>\n",
    "（claw 执行后把 [tool_results] 回灌）\n",
    "助手（第 2 轮，看到目录里有 novels/）：<tool_call>{\"name\":\"fs_list\",\"arguments\":{\"path\":\"novels\"}}</tool_call>\n",
    "（再次回灌）\n",
    "助手（第 3 轮）：你的卡里有 3 本小说：……（自然语言总结，无 <tool_call>，循环结束）\n",
    "\n",
    "用户：我感觉嘴起皮了，是不是空气好干。\n",
    "助手：我先读取一下温湿度。<tool_call>{\"name\":\"sensor_read_temp_humidity\",\"arguments\":{}}</tool_call>\n",
    "（claw 回灌 [tool_results]）\n",
    "助手：我看了下温湿度传感器，现在湿度确实偏低，空气有点干，你可以喝点水或暂时远离空调直吹。\n",
    "\n",
    "用户：把今天的日记写到 notes/diary.md\n",
    "助手：好的，我把日记追加到 notes/diary.md。<tool_call>{\"name\":\"fs_write\",\"arguments\":{\"path\":\"notes/diary.md\",\"content\":\"2026-05-15 ...\\n\",\"append\":true}}</tool_call>\n",
    "\n",
    "用户：等湿度高于 80 时给我发飞书消息。\n",
    "助手：好的，我创建一条 Rust 原生告警规则，每 5 秒查一次湿度，第一次超过 80 就发飞书并自动停用。<tool_call>{\"name\":\"monitor_create\",\"arguments\":{\"field\":\"humidity_pct\",\"op\":\"gt\",\"threshold\":80,\"action\":\"feishu_send\",\"message\":\"湿度已达 {value}%RH，超过阈值 80\",\"every_secs\":5,\"fire_once\":true}}</tool_call>\n",
    "（claw 回灌 [tool_results]，确认规则已创建）\n",
    "助手：已设置好，等湿度超过 80%RH 时我会给你发一条飞书消息。\n",
    "\n",
    "用户：把一号灯亮起来。\n",
    "助手：我先看一下板子有哪些灯。<tool_call>{\"name\":\"led_list\",\"arguments\":{}}</tool_call>\n",
    "（回灌后看到列表里有 led1）\n",
    "助手：好的，点亮 led1。<tool_call>{\"name\":\"led_set\",\"arguments\":{\"name\":\"led1\",\"state\":\"on\"}}</tool_call>\n",
    "（回灌确认）\n",
    "助手：一号灯已点亮。\n",
    "\n",
    "用户：温度超过 35 度时把一号灯（led1）亮起来。\n",
    "助手：好的，我建一条规则：每 5 秒查一次温度，超过 35°C 时由 Rust 直接点亮 led1，触发一次后停用。<tool_call>{\"name\":\"monitor_create\",\"arguments\":{\"field\":\"temp_c\",\"op\":\"gt\",\"threshold\":35,\"action\":\"led_set\",\"led\":\"led1\",\"state\":\"on\",\"every_secs\":5,\"fire_once\":true}}</tool_call>\n",
    "（回灌确认）\n",
    "助手：已设置，等温度超过 35°C 我会立刻点亮 led1。\n",
    "\n",
    "用户：把灯带调成慢一点的彩虹。\n",
    "助手：好的，开启慢速彩虹。<tool_call>{\"name\":\"strip_effect\",\"arguments\":{\"effect\":\"rainbow\",\"speed\":2}}</tool_call>\n",
    "（回灌确认）\n",
    "助手：灯带已切到慢速彩虹效果。\n",
    "\n",
    "用户：灯带改成红色常亮。\n",
    "助手：好的。<tool_call>{\"name\":\"strip_effect\",\"arguments\":{\"effect\":\"solid\",\"color\":\"red\"}}</tool_call>\n",
    "（回灌确认）\n",
    "助手：灯带已设为红色常亮。\n",
    "\n",
    "用户：让灯带蓝色闪烁。\n",
    "助手：好的，蓝色闪烁。<tool_call>{\"name\":\"strip_effect\",\"arguments\":{\"effect\":\"blink\",\"color\":\"blue\",\"period_ms\":500}}</tool_call>\n",
    "（回灌确认）\n",
    "助手：灯带已开始蓝色闪烁。\n",
);

pub fn format_task_chat_input(task_title: &str) -> String {
    format!("{}{}", TASK_EXECUTION_PROMPT_PREFIX, task_title.trim())
}

pub fn section_or_placeholder<'a>(content: &'a str, placeholder: &'a str) -> &'a str {
    let trimmed = content.trim();
    if trimmed.is_empty() {
        placeholder
    } else {
        trimmed
    }
}

pub fn format_pc_system_prompt(
    soul: &str,
    user: &str,
    role: &str,
    summary_memories: &str,
) -> String {
    format!(
        "{}\n\n[Soul]\n{}\n\n[User Description]\n{}\n\n[Role]\n{}\n\n[Summary Memories]\n{}",
        PC_RUNTIME_DESCRIPTION,
        section_or_placeholder(soul, EMPTY_SOUL_PLACEHOLDER),
        section_or_placeholder(user, EMPTY_USER_PLACEHOLDER),
        section_or_placeholder(role, EMPTY_ROLE_PLACEHOLDER),
        section_or_placeholder(summary_memories, SUMMARY_MEMORY_EMPTY_TEXT),
    )
}

pub fn format_mcu_system_prompt(
    soul: &str,
    user: &str,
    role: &str,
    memory: &str,
) -> String {
    format!(
        "{}\n{}\n{}\n{}\n长期记忆：\n{}{}",
        soul,
        user,
        role,
        MCU_RUNTIME_DESCRIPTION,
        memory,
        TOOLS_PROMPT_SECTION,
    )
}
