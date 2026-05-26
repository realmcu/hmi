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
