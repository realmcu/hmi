# External 8711 Wi-Fi (AT-over-SPI) attach skeleton

> ## ⚠️ Historical document -- three updates stacked on top of each other, and the last one is now inverted
>
> Know the current facts before reading: **the 8711 runs Realtek's standard AT firmware, and this side is the SPI host**,
> node `&spi1_hs` + DMA 6/7 + mode 0 + 25 MHz, handshake lines **m2s (P2_7 output) / s2m
> (P4_1 input)**, slots are a fixed 16384 B + 4-byte slot header + **no CRC**.
>
> So the three layers of conclusions in this document should be read like this:
>
> - **Body (2026-08-14, host skeleton)**: the master/slave relationship is back to host, which is **right** in direction; but every file path
>   is wrong -- `app/wifi_8711/`, `snippets/wifi_8711/`, `dts/bindings/spi_m2s2m-gpio.yaml`
>   have all been deleted, and the live ones are `app/spi_at_master/` + `snippets/wifi/`.
>   Item 3 of "TODO" describes `[AT(2)][len(2)][data(N)]` + `SPI_XMIT_SIZE 16 KiB`,
>   which is actually **close to** the real slot format now (except that there is no trailing CRC32).
> - **2026-08-18 update block (ATMC / we act as slave)**: **the whole block is obsolete**; the ATMC protocol no longer exists.
> - **2026-08-19 update block (the 🚫 one)**: **it is now inverted**. It says "switched to `&spi0_slave` +
>   normal-group psel + DMA 55/56 + `b2w`/`w2b`" -- those are **the dead ones**; `&spi1_hs` + HS-group
>   psel + DMA 6/7 + `m2s`/`s2m` are the live ones. The only part still valid is the last sentence:
>   **the P4_3/P4_4 hardware conflict with touch was solved by the hardware moving I2C2 to P2_3/P2_4**.
>
> The "three non-obvious pitfalls" (you must touch the DT, the `psram1_nc` node silently does nothing if missing, DMA channels 0/1 are free)
> **are all still valid**, and that is the main value this document retains. For the current form see
> [protocol-reference.md](protocol-reference.md) sections 3 and 5.

2026-08-14. **Attach skeleton only**: devicetree + Kconfig + CMake wired up, so an overlay typo is caught at build time and a bus that fails to come up is caught at boot time. **Contains no code that talks to the chip**; the AT engine, SPI DMA transfer and socket flow all still need porting (see the TODO at the end).

Both builds pass (`EXITCODE=0`, 2026-08-14): baseline without the snippet, and `-S wifi_8711` pristine 445/445.

> ⚠️ **2026-08-18 update**: the 8711FA firmware the vendor supplied afterwards uses the **ATMC protocol, with the master/slave roles opposite to this skeleton** (the 8711 is the SPI host and the 8773G is the slave). **The "pin assignment" table, the `&spi1_hs` host-mode overlay, and items 1/3/5/6 of the porting plan in "TODO" in this document all do not apply to that firmware** -- see `note/spi-at-atmc-protocol.md`. This skeleton is still usable as a reference for the snippet / DT / CMake attach method, and the "three non-obvious pitfalls" all remain valid.

> 🚫 **2026-08-19: the code state described in this document no longer exists.** The skeleton was converted wholesale to the v2.0 protocol with the local side as slave and compiles (449/449): the overlay switched to `&spi0_slave` + four normal-group `SPI_*_SLAVE` psel entries (P4_2..P4_5) + DMA 55/56, the handshake lines were renamed from `m2s`/`s2m` to **`b2w`/`w2b`** (the old names referred to "who is master", so a role swap silently inverts them), the bindings `dts/bindings/spi_m2s2m-gpio.yaml` and `wifi_8711-device.yaml` **were deleted** (a slave has no bus peer), `dts/bindings/wifi_8711-irq.yaml` was added, the Kconfig `WIFI_8711_ROLE_MASTER` became `_ROLE_SLAVE`, and the shell `m2s` command became `b2w`. **Every mention of those file names/symbols below has historical value only.** For the current implementation see `app/wifi_8711/` and `snippets/wifi_8711/`; for the protocol see `note/refer/spi-at-command-protocol 1.md`. Also: the "P4_3/P4_4 clash with touch" hardware conflict at the end **has been solved by the hardware moving I2C2 to P2_3/P2_4**.

## Why the device tree has to be touched

`CONFIG_SPI_RTL87X3G` is `default y`, but it `depends on DT_HAS_REALTEK_RTL87X3G_SPI_ENABLED`. And in `rtl87x3g.dtsi:343-396` the five SPI nodes (`spi0` / `spi0_slave` / `spi1` / `spi1_hs` / `spi2`) are **all `status="disabled"`**. Without an overlay the SPI driver does not take part in the build at all, and `CONFIG_SPI=y` changes nothing.

Also `realtek,rtl87x3g-spi.yaml` lists `pinctrl-0` and `pinctrl-names` as **required** -- to enable the controller you must supply a pinctrl group as well; the two cannot be separated.

## Pin assignment (user-specified)

| Signal | Pad | Notes |
|---|---|---|
| SPI CLK | P4_2 | `HS_P4_2_SPI1_CLK` |
| SPI MISO | P4_3 | `HS_P4_3_SPI1_MISO` |
| SPI MOSI | P4_4 | `HS_P4_4_SPI1_MOSI` |
| SPI CS | P4_5 | `HS_P4_5_SPI1_CSN` (hardware SS_N, not a GPIO CS) |
| BLE→WIFI IRQ (M2S) | P4_1 | `&gpiob 8`, `GPIO_ACTIVE_LOW`, output |
| WIFI→BLE IRQ (S2M) | P2_7 | `&gpioa 22`, `GPIO_ACTIVE_HIGH`, interrupt input |

The pad→GPIO mapping comes from `zephyr/include/zephyr/dt-bindings/pinctrl/rtl87x3g-pinctrl.h`: `P4_1 = 29 /* GPIOB8 */`, `P2_7 = 21 /* GPIOA22 */`. `gpioa` has `port=<0>` and `gpiob` has `port=<1>`, so P4_1 is written as `<&gpiob 8 ...>`.

All four psel entries use `DIR_OUT` (MISO included) -- this is not a typo; the snippets of the same name in `applications/watch` and `applications/bt_audio_trx` are identical, and under HS pinmux the signal direction is decided by the pad function.

The evb board **has no `board.h`** (only `rtl87x3g_watch` / `rtl87x3g_bt_audio_trx` define macros such as `PIN_SPI1_CLK`), so the second argument of `RTL87X3G_PSEL` is the bare pad name `P4_2`, consistent with the style of the existing overlays in this project.

## File list

```
snippets/wifi_8711/wifi_8711.overlay    DT: spi1_hs + bus peer + handshake GPIO + psram1_nc
snippets/wifi_8711/wifi_8711.conf       CONFIG_WIFI_8711 / SPI / DMA / RING_BUFFER
snippets/wifi_8711/snippet.yml          append EXTRA_CONF_FILE + EXTRA_DTC_OVERLAY_FILE
dts/bindings/spi_m2s2m-gpio.yaml        binding for the handshake GPIO
dts/bindings/wifi_8711-device.yaml      binding for the SPI bus peer (includes spi-device.yaml)
app/wifi_8711/Kconfig                   WIFI_8711 / _ROLE_MASTER / _TEST
app/wifi_8711/CMakeLists.txt            file(GLOB) wrapped in if(CONFIG_WIFI_8711)
app/wifi_8711/wifi_8711.{h,c}           init/ready + 6 TODOs to port
app/wifi_8711/wifi_8711_shell.c         wifi8711 init / info / m2s
Kconfig                                  added rsource "app/wifi_8711/Kconfig"
```

**It is discovered with zero CMake changes**: `snippets.cmake:60` automatically appends `APPLICATION_SOURCE_DIR` to `SNIPPET_ROOT`, and `pre_dt.cmake:41-47` likewise appends `DTS_ROOT`. So putting `snippets/` and `dts/bindings/` under the app root is enough.

## Build commands

`note/build.ps1` already has a `-Snippet` parameter (several may be passed, stacked with `-Bank`):

```powershell
.\note\build.ps1 -Snippet wifi_8711            # -> build_wifi_8711\,        bin\app.bin
.\note\build.ps1 -Snippet wifi_8711 -Pristine  # mandatory after adding a .c file (see below)
.\note\build.ps1 -Bank 0 -Snippet wifi_8711    # -> build_bank0_wifi_8711\,  bin\bank0\app_bank0.bin
```

The snippet combination takes part in build directory naming, so with and without Wi-Fi each gets its own directory; switching between them does not hit Zephyr's "snippet set changed, must be pristine" restriction, and the default `build\` is not polluted either.

The equivalent raw west command (this is exactly what the script assembles):

```powershell
$env:PYTHONUTF8="1"
$env:ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR="D:\Project\HoneyHmi\bb2u\zephyr-sdk-0.16.8_windows-x86_64_minimal\zephyr-sdk-0.16.8"
Remove-Item Env:\GNUARMEMB_TOOLCHAIN_PATH -ErrorAction SilentlyContinue
python -m west build -b rtl87x3g_evb -d build_wifi_8711 --snippet wifi_8711 -p always
```

`app/wifi_8711/CMakeLists.txt` uses `file(GLOB)`, so **after adding a .c file a pristine rebuild is mandatory** (the same pitfall as `app/protocol/CMakeLists.txt`).

## Three non-obvious pitfalls

### 1. P4_3 / P4_4 physically clash with the touch panel (no software fix)

These two pads are also **I2C2_DAT / I2C2_CLK**, connected to the CST816D touch panel. Whoever runs pinctrl last owns the pad; **touch and Wi-Fi cannot work at the same time with this wiring, and it must be solved on the hardware side**.

The overlay **deliberately does not disable `&i2c2`**: `port/ui/gui_port_indev.c:16-18` uses an unguarded `DEVICE_DT_GET(DT_NODELABEL(touch_device))` with no `DT_NODE_HAS_STATUS` wrapper, so disabling it turns into a `__device_dts_ord_NN` link error (the same class of pitfall as when enabling the RTC); it is not cleanly excluded at compile time.

### 2. The `psram1_nc` node exists nowhere in the repository, so `SECTION_PSRAM1_NC` silently fails

`SECTION_PSRAM1_NC` is defined only in `applications/watch/src/app/psram_section.h`, and it is wrapped in `DT_NODE_EXISTS(DT_NODELABEL(psram1_nc))` -- when the node does not exist it **expands to nothing**, without an error. The roughly 114 KB of SPI buffers would land straight in the 82 KB DTCM → link overflow.

So the overlay adds a `zephyr,memory-region`:

```
psram1_nc: memory@22380000 { reg = <0x22380000 DT_SIZE_K(512)>; ... };
```

The address is derived, not guessed: `SPIC1_MEM_BASE = 0x22000000` (`address_map.h:16`); in `gui_port_os.c:24,142-143` `DSP_RSV_SIZE = 512K`, and the GUI lower heap starts at `SPIC1_MEM_BASE + DSP_RSV_SIZE`, that is `0x300000`. So `0x22380000..0x22400000`, that 512 KB, is free. And it falls inside MPU region 2 of `app_lower_init.c:27-34` (`0x22000000..0x22400000`, attr `0x44` = non-cacheable), so **DMA does not need `SCB_CleanDCache`** -- which conveniently avoids the H264 L1 D-cache write-back issue.

The link has been confirmed to contain a `PSRAM1_NC: 512 KB` region.

### 3. DMA channels 0/1 are free

`uart3` occupies 2/3 (`dmas = <&dma0 2 49 0xa>, <&dma0 3 48 0x10021>`), so channels 0/1 are used for SPI, matching the allocation in watch.

## Baseline breakage fixed along the way (unrelated to this skeleton)

The baseline already failed to build before the skeleton went in; this was the seventh missed call site from the V1.2 migration:

```
app/designer/src/user/ViewMainFace_user.c:218
  → app/bluetooth/hmi_ble/hmi_ble_central.h:24
  → fatal error: hmi_l2_xfer_client.h: No such file or directory
```

In its non-simulator branch, `ViewMainFace_user.c` includes three headers from directories that have been dropped from the build: `hmi_ble_central.h`, `hmi_l2.h` (in `component/protocol/`) and `hmi_l2_cmd_remote.h` (in `app/l2_handlers/`). The `CMakeLists.txt` of both of those directories now contains only comments; they neither build sources nor provide include paths.

Following the existing `#if 0 /* V1.2 migration: ... parked */` pattern, 4 places were parked (**the old code is not deleted**): the include block + `done_cb`, the progress read in `prog_arc_timer`, the getter of `on_remote_state_changed`, and the three registrations in `app_remote_ctrl_init`. The progress ring degrades to a fixed minimum angle; the three remote callback bodies are kept and referenced with `(void)` to silence the unused warning. Line 204 of the note's stalled-work table previously claimed this spot had been parked, but it had not; that is corrected.

## TODO (all still empty)

> ⚠️ **The plan below ("port 3700 lines from watch") has been overturned by `note/spi-at-atmc-protocol.md`** (the vendor's actual firmware uses the ATMC protocol, with the 8773G as slave). The original text is kept to record the reasoning at the time; for the first thing that actually needs doing see section 9.1, "minimal slave bring-up experiment", of that note. The question in item 6 now has an answer: an AP-mode command does exist (`AT+WLSTARTAP`, enabled at boot), but in a different firmware and a different protocol.

### Porting (in this order; the source is in `applications/watch/src/module/wifi_8711/`, about 3700 lines)

1. **Power-on / RF bring-up** -- not in the devicetree. `applications/eBadge` does it with raw `Pad_Config` / `Pinmux_Config`: pull WIFI_EN low for 200 ms → high → wait 2 s for the AT firmware to come up; then configure the RF switch's `EN_EXLNA` / `EN_EXPA`. **The 8711's own timing must be taken from its datasheet; do not copy another IC's.**
2. **`app_spi_master_zephyr.c`** -- TX ring + ping-pong RX in `SECTION_PSRAM1_NC`, async DMA transfers serialised with an idle flag, S2M interrupt → gpio callback → post to the task, M2S toggled around every transfer. **The `ring_buf` control structure must stay in on-chip RAM**: there is no LDREX/STREX on PSRAM.
3. **`app_spi_atcmd.c`** -- frame as `[AT(2)][len(2)][data(N)][crc32(4)][pad]` with `SPI_FRAME_CRC_EN 0` (the ameba slave does not append a checksum), a command/response table with per-command timeouts, and the two-stage `AT+SKTSENDRAW` (send command → wait for `">>>"` → push raw data → wait for `"OK"`). `SPI_XMIT_SIZE = 16 KiB`.
4. **Drop the watch-specific dependencies** -- `app_cmd.h` / `app_dlps.h` / `trace.h` / `rtk_errno.h`; `psram_section.h` lives under `applications/watch/src/app/`, so copy it over or inline the `SECTION_PSRAM1_NC` macro.
5. **Make `ebadge_port_tcp` an AT adapter** -- `listen` → `AT+SKTCFG` + `AT+SKTSERVER`; `on_data` ← non-request RX frames; `send` → `AT+SKTSENDRAW`; `close` → `AT+SKTDEL`. The port_tcp contract requires each callback to accumulate at least 4 KB, which a 16 KB SPI frame satisfies naturally.
6. **`ebadge_port_softap` is an open problem** -- in the watch module **only `AT+WLCONN` (station mode) was found; there is no AP-mode command**. Yet eBadge V1.2 needs the device to **act as an AP** itself. Before starting, confirm against the 8711 AT firmware documentation whether port_softap can be implemented at all.

### Hardware / verification

- The P4_3/P4_4 pad conflict with the touch panel needs a hardware decision (re-route or pick one).
- The skeleton only goes as far as boot-time checks; the three shell commands `wifi8711 init` / `info` / `m2s <0|1>` have not been run on the board yet. The `m2s` one, together with a scope probe on P4_1, is used to tell "we configured the pin wrong" from "the chip does not respond".
- The TCP/IP stack is in the 8711 firmware; there is **no lwIP and no Zephyr `net_if`** on the SoC, and the sockets are all AT commands.
