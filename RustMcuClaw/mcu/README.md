# RustMcuClaw MCU Version

This directory is the RTL8783G/Zephyr MCU version of `RustMcuClaw`, without a GUI.

## Features

- Serial CLI: a PC serial tool can chat directly with Claw on the EVB.
- Zephyr multithreading: the `claw_cli` thread handles serial line input, and the `claw_poll` thread handles heartbeat and scheduled tasks.
- PSRAM heap: the Rust `no_std + alloc` static library initializes the bump allocator using `RUSTMCUCLAW_PSRAM_BASE` / `RUSTMCUCLAW_PSRAM_SIZE`.
- SD card files: default path `/SD:/RustMcuClaw`, with file formats following the PC version:
  - `rmcc.toml`
  - `soul.md`
  - `user.md`
  - `role.md`
  - `summary_memory.jsonl`
  - `tasks.json`
- Cloud LLM: Rust generates OpenAI-compatible JSON requests, and calls the Z2Plus HTTPS capability through the C callback `claw_mcu_https_post_json()`.

## Z2Plus HTTPS Integration Point

The public examples found in the current SDK are mainly the Z2Plus AT/TCP channel (such as `ATPN`, `ATWT`, `ATSD`) and MQTT transparent TCP examples; the final function name of a generic HTTPS command was not found. Therefore the MCU app already provides a strong-symbol override point:

- Default weak implementation: `z2plus_https_post_json()` in `mcu/src/main.c` returns `-ENOTSUP`.
- If the Z2Plus firmware provides an HTTPS POST API, implement a non-weak function with the same name in a board-level file:
  - Input: OpenAI-compatible endpoint, API key, JSON body.
  - Output: the raw HTTP response JSON, for example in OpenAI chat-completions format.

When `[provider].kind = "mock"` in `rmcc.toml`, the network is not accessed, so you can first verify serial, SD card, tasks, and the Rust heap.

## Build

Install the Rust target first:

```powershell
rustup target add thumbv8m.main-none-eabihf
```

Build this directory's application from the SDK/Zephyr build environment, for example:

```powershell
west build -b rtl87x3g_watch/rtl8783gbf realtek-app/applications/claw/RustMcuClaw/mcu
```

If the board PSRAM address or size differs, override it in CMake/build parameters:

- `RUSTMCUCLAW_PSRAM_BASE`
- `RUSTMCUCLAW_PSRAM_SIZE`
- `RUSTMCUCLAW_DATA_DIR`
- `RUSTMCUCLAW_UNIX_EPOCH_OFFSET`
