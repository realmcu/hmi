# RustMcuClaw MCU 版

这个目录是 `RustMcuClaw` 的 RTL8783G/Zephyr MCU 版，不包含 GUI。

## 功能

- 串口 CLI：PC 串口工具直接和 EVB 上的 Claw 对话。
- Zephyr 多线程：`claw_cli` 线程处理串口行输入，`claw_poll` 线程处理 heartbeat 和定时任务。
- PSRAM 堆：Rust `no_std + alloc` 静态库使用 `RUSTMCUCLAW_PSRAM_BASE` / `RUSTMCUCLAW_PSRAM_SIZE` 初始化 bump allocator。
- SD 卡文件：默认路径 `/SD:/RustMcuClaw`，文件格式沿用 PC 版：
  - `rmcc.toml`
  - `soul.md`
  - `user.md`
  - `role.md`
  - `summary_memory.jsonl`
  - `tasks.json`
- 云 LLM：Rust 生成 OpenAI-compatible JSON 请求，通过 C 回调 `claw_mcu_https_post_json()` 调用 Z2Plus HTTPS 能力。

## Z2Plus HTTPS 对接点

当前 SDK 搜索到的公开例程主要是 Z2Plus AT/TCP 通道（如 `ATPN`、`ATWT`、`ATSD`）和 MQTT 透明 TCP 示例，未发现通用 HTTPS 命令的最终函数名。因此 MCU app 已经预留强符号覆盖点：

- 默认弱实现：`mcu/src/main.c` 中的 `z2plus_https_post_json()` 返回 `-ENOTSUP`。
- 如果 Z2Plus 固件提供 HTTPS POST API，请在板级文件里实现同名非弱函数：
  - 输入：OpenAI-compatible endpoint、API key、JSON body。
  - 输出：HTTP 响应 JSON 原文，例如 OpenAI chat-completions 格式。

`rmcc.toml` 中 `[provider].kind = "mock"` 时不访问网络，可先验证串口、SD 卡、任务和 Rust 堆。

## 构建

先安装 Rust 目标：

```powershell
rustup target add thumbv8m.main-none-eabihf
```

从 SDK/Zephyr build 环境中构建本目录应用，示例：

```powershell
west build -b rtl87x3g_watch/rtl8783gbf realtek-app/applications/claw/RustMcuClaw/mcu
```

如板级 PSRAM 地址或大小不同，可在 CMake/编译参数中覆盖：

- `RUSTMCUCLAW_PSRAM_BASE`
- `RUSTMCUCLAW_PSRAM_SIZE`
- `RUSTMCUCLAW_DATA_DIR`
- `RUSTMCUCLAW_UNIX_EPOCH_OFFSET`
