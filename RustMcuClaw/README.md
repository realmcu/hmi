# RustMcuClaw

RustMcuClaw is a PC terminal agent written in Rust, with features similar to OpenClaw: it supports terminal chat, cloud LLM, summarized memory, scheduled tasks, heartbeat, soul files, user profile files, and role files.

This project now also includes an MCU version: `mcu/` is the RTL8783G/Zephyr application, and `crates/rustmcuclaw-mcu/` is a `no_std + alloc` Rust static library.

## Features

- Memory safety: The PC version's business logic uses safe Rust; the MCU version uses the necessary `unsafe` only at FFI, PSRAM allocator, and C string boundaries.
- Terminal chat: `rustmcuclaw chat`, or run it directly to enter chat by default.
- Cloud LLM: Supports OpenAI, Kimi, GitHub Models, and OpenAI-compatible endpoints. GitHub Copilot-related capabilities are accessed through a user-provided compatible endpoint/token; it does not read or bypass VS Code Copilot credentials.
- Summarized memory: After each complete chat, it calls the LLM to generate a summary of no more than 50 characters, appends it to JSONL, and loads it as part of the system prompt in new chats.
- Scheduled tasks: Supports periodic tasks; when a task is due, the task text is automatically sent to Claw as a chat message. The minimum period is 10 minutes.
- Heartbeat: Periodically prints a liveness signal in the background.
- Persona files: `soul.md`, `user.md`, and `role.md` are combined into the system prompt.

## Quick Start

```powershell
cargo run -- init
cargo run -- chat
```

In chat you can use:

- `/help`
- `/doctor`
- `/task list`
- `/task add 600 drink water`
- `/task done <id>`
- `/exit`

## Command Line

```powershell
cargo run -- ask "Hello, introduce yourself"
cargo run -- task add "check device heartbeat" --every 600
cargo run -- task list
cargo run -- doctor
```

## Configuring the LLM

After initialization, edit `config/rmcc.toml`.

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

### OpenAI-compatible Service / GitHub Copilot Compatible

```toml
[provider]
kind = "open_ai_compatible"
model = "your-model"
endpoint = "https://your-endpoint/v1/chat/completions"
api_key_env = "YOUR_API_KEY_ENV"
temperature = 0.7
max_tokens = 2048
```

## Data Files

The default data directory is determined by the system; if the example configuration is used, it is under the project's `data/`:

- `soul.md`: Assistant soul settings
- `user.md`: User description
- `role.md`: Role and behavior constraints
- `summary_memory.jsonl`: Summarized memory
- `tasks.json`: Scheduled tasks

## Build

```powershell
cargo build --release
```

## MCU Version (RTL8783G / Zephyr)

The MCU version aims to chat directly through a serial tool on the EVB, without a GUI:

- Serial CLI: The Zephyr `claw_cli` thread reads line input from the serial port and outputs answers.
- Cloud LLM: Rust generates OpenAI-compatible JSON, and the C callback `claw_mcu_https_post_json()` connects to Z2Plus HTTPS.
- SD card filesystem: The default data directory is `/SD:/RustMcuClaw`, with file names consistent with the PC version.
- Zephyr multithreading: Separate CLI thread and poll/scheduled task thread.
- PSRAM heap: The Rust MCU crate is initialized with PSRAM by `rustmcuclaw_mcu_init()` to set up the global allocator.

First install the Rust MCU target:

```powershell
rustup target add thumbv8m.main-none-eabihf
```

Check the Rust MCU static library:

```powershell
cargo check -p rustmcuclaw-mcu --target thumbv8m.main-none-eabihf
```

Build the Zephyr app:

```powershell
west build -b rtl87x3g_watch/rtl8783gbf realtek-app/applications/claw/RustMcuClaw/mcu
```

Note: The current SDK already contains Z2Plus `ATPN`/`ATWT`/transparent TCP related code, but the final public API for generic HTTPS POST has not been found; therefore `mcu/src/main.c` provides a weak symbol `z2plus_https_post_json()`. To actually integrate with the Z2Plus HTTPS firmware, implement a non-weak function with the same name to replace the placeholder implementation that returns `-ENOTSUP` by default.
