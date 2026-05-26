# RustMcuClaw

RustMcuClaw 是一个用 Rust 编写的 PC 终端智能体，功能类似 OpenClaw：支持终端聊天、云端 LLM、总结记忆、定时任务、心跳、soul 文件、用户描述文件和角色文件。

本工程现在同时包含 MCU 版：`mcu/` 是 RTL8783G/Zephyr 应用，`crates/rustmcuclaw-mcu/` 是 `no_std + alloc` Rust 静态库。

## 特性

- 内存安全：PC 版业务逻辑使用安全 Rust；MCU 版仅在 FFI、PSRAM allocator 和 C 字符串边界使用必要的 `unsafe`。
- 终端聊天：`rustmcuclaw chat` 或直接运行默认进入聊天。
- 云端 LLM：支持 OpenAI、Kimi、GitHub Models、OpenAI-compatible endpoint。GitHub Copilot 相关能力通过用户提供的兼容 endpoint/token 接入，不读取或绕过 VS Code Copilot 凭据。
- 总结记忆：每次完整聊天结束后，调用 LLM 生成不超过 50 字的总结，追加保存到 JSONL，并在新聊天中作为系统提示词的一部分加载。
- 定时任务：支持周期任务；任务到期后会自动把任务文本作为一条聊天发给 Claw，最小周期 10 分钟。
- 心跳：后台周期打印存活信号。
- 人设文件：`soul.md`、`user.md`、`role.md` 会组合成系统提示词。

## 快速开始

```powershell
cargo run -- init
cargo run -- chat
```

在聊天中可使用：

- `/help`
- `/doctor`
- `/task list`
- `/task add 600 喝水`
- `/task done <id>`
- `/exit`

## 命令行

```powershell
cargo run -- ask "你好，介绍一下自己"
cargo run -- task add "检查设备心跳" --every 600
cargo run -- task list
cargo run -- doctor
```

## 配置 LLM

初始化后编辑 `config/rmcc.toml`。

### OpenAI

```toml
[provider]
kind = "open_ai"
model = "gpt-4o-mini"
api_key_env = "OPENAI_API_KEY"
temperature = 0.7
max_tokens = 2048
```

### Kimi

```toml
[provider]
kind = "kimi"
model = "moonshot-v1-8k"
api_key_env = "KIMI_API_KEY"
temperature = 0.7
max_tokens = 2048
```

### GitHub Models

```toml
[provider]
kind = "github_models"
model = "openai/gpt-4o-mini"
api_key_env = "GITHUB_TOKEN"
temperature = 0.7
max_tokens = 2048
```

### OpenAI 兼容服务 / GitHub Copilot Compatible

```toml
[provider]
kind = "open_ai_compatible"
model = "your-model"
endpoint = "https://your-endpoint/v1/chat/completions"
api_key_env = "YOUR_API_KEY_ENV"
temperature = 0.7
max_tokens = 2048
```

## 数据文件

默认数据目录由系统决定；如果使用示例配置，则在项目 `data/` 下：

- `soul.md`：助手灵魂设定
- `user.md`：用户描述
- `role.md`：角色和行为约束
- `summary_memory.jsonl`：总结记忆
- `tasks.json`：定时任务

## 构建

```powershell
cargo build --release
```

## MCU 版（RTL8783G / Zephyr）

MCU 版目标是在 EVB 上通过串口工具直接聊天，不需要 GUI：

- 串口 CLI：Zephyr `claw_cli` 线程读取串口行输入并输出回答。
- 云 LLM：Rust 生成 OpenAI-compatible JSON，由 C 回调 `claw_mcu_https_post_json()` 对接 Z2Plus HTTPS。
- SD 卡文件系统：默认数据目录 `/SD:/RustMcuClaw`，文件名和 PC 版一致。
- Zephyr 多线程：独立 CLI 线程和 poll/定时任务线程。
- PSRAM 堆：Rust MCU crate 由 `rustmcuclaw_mcu_init()` 使用 PSRAM 初始化全局 allocator。

先安装 Rust MCU target：

```powershell
rustup target add thumbv8m.main-none-eabihf
```

检查 Rust MCU 静态库：

```powershell
cargo check -p rustmcuclaw-mcu --target thumbv8m.main-none-eabihf
```

构建 Zephyr app：

```powershell
west build -b rtl87x3g_watch/rtl8783gbf realtek-app/applications/claw/RustMcuClaw/mcu
```

注意：当前 SDK 中已找到 Z2Plus `ATPN`/`ATWT`/透明 TCP 相关代码，但未找到通用 HTTPS POST 的最终公开 API；因此 `mcu/src/main.c` 提供了弱符号 `z2plus_https_post_json()`。实际接入 Z2Plus HTTPS 固件时，实现同名非弱函数即可替换默认返回 `-ENOTSUP` 的占位实现。
