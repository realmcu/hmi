# z2plus TCP/UDP Transfer

PC side helper for the z2plus firmware `ATTF` command.

This folder now also contains a minimal RTL87X3G host application that:

- powers up z2plus
- sends `ATTF=tcp,5001` on `uart3`
- receives the forwarded TCP byte stream from z2plus over SDIO
- reconstructs the `Z2FT1 TCP` header + file payload stream
- verifies the final CRC32 and prints the result

Current firmware scope:

- TCP receive path only
- stream receive and CRC validation only
- no 8189 NIC dependency
- no file persistence yet; payload is consumed as a stream and verified in place

Build example:

```powershell
Set-Location C:\Users\triton_yu\Documents\hmi\zephyrproject
west build -s realtek-app/applications/z2plusTCPUDP -b rtl87x3g_watch/rtl8783gbf -d build/z2plusTCPUDP
```

Build output:

- Build directory: `zephyrproject/build/z2plusTCPUDP`
- Final flashable image: `zephyrproject/bin/app.bin`
- MP header image: `zephyrproject/bin/app_MP-v01.00.00.01-00000000-<hash>.bin`
- ELF/map/list files: `zephyrproject/bin/app.elf`, `zephyrproject/bin/app.map`, `zephyrproject/bin/app.lst`

Notes:

- Run the command from the project root `C:\Users\triton_yu\Documents\hmi\zephyrproject`.
- `west build` compiles the application into `zephyrproject/build/z2plusTCPUDP`, then the post-build step copies the final artifacts into `zephyrproject/bin`.

Runtime flow:

1. Flash and boot the `z2plusTCPUDP` app on RTL87X3G.
2. The host app powers z2plus and sends `ATTF=tcp,5001` through `uart3`.
3. Run the PC helper below to connect to z2plus and send the file.
4. Watch the RTL87X3G log for `transfer start` / `transfer done` and CRC status.

Board commands:

- `ATTF=tcp,5001`
- `ATTF=udp,5002`
- `ATTF=stop`

Protocol:

- TCP header: `Z2FT1 TCP <filename> <size> <crc32_hex>`
- UDP header: `Z2FT1 UDP <filename> <size> <payload_size> <crc32_hex>`
- UDP data: `<seq_le32><payload>`
- UDP end: `Z2FT1 END`

Examples:

```powershell
python realtek-app/applications/z2plusTCPUDP/pc_transfer.py --mode tcp --host 172.20.10.5 --port 5001 --file realtek-app/applications/z2plusTCPUDP/test.bin

```