# tools/pack —— app/romfs 合并打包 + 预制资源 FDB 打包

两个目标：

1. **app 与 UI 资源作为同一个镜像发布**，OTA 一次到达、版本天然一致。
2. **预制壁纸以 FlashDB 格式直接烧到 FDB 区**，开机由 `flashdb_prepare()` 枚举出来。

所有地址来自 [`layout.json`](layout.json)。改 flash 规划后先跑 `check`。

---

## 1. `check` —— 布局一致性

```bash
python3 tools/pack/pack.py check
```

把 `layout.json` 和工程里**各处硬编码**的地址逐一比对：

| 比对对象 | 位置 |
|---|---|
| `BANK{0,1}_APP_ADDR/SIZE` | `flash_map.h` |
| app 分区 | 本工程 `snippets/flash_32M_bank{0,1}/*.overlay`（当前使用）<br>`realtek-app/snippets/flash_{4,8,16}M_bank{0,1}/*.overlay`（备用变体） |
| FDB 区绝对地址/大小 | `port/flashdb/fal_cfg.h` |
| romfs 挂载 | `app/designer/src/Mi_eBadgeEntry.c`、`app/designer/project.json` |

只报告不一致，不判断谁对——谁对由规划决定。当前 `layout.json` 与工程各处一致。

---

## 2. `app` —— 合并 app + romfs

```bash
python3 tools/pack/pack.py app \
    --bank bank0 \
    --app bin/bank0/app_bank0.bin \
    --romfs app/designer/build/app_romfs_0x70536400.bin \
    --out bin/bank0/app_bank0_packed.bin \
    --sign
```

做的事，按顺序：

1. 校验输入是带镜像头的 bin（`IMGHDR`），且 `payload_len + 0x400 == 文件长度`。
2. 计算 romfs 落点 = `app 基址 + 偏移`，检查不重叠、64 对齐。
3. **重定位 romfs**：mkromfs 把 `name` / `data` 写成**绝对 flash 地址**，
   换落点后必须整体平移，否则目录项解引用到野地址。工具递归遍历所有可达目录项
   并 `+= delta`，然后检查每个指针都落在本 romfs 内。
4. 拼成 `app ++ 填充 ++ romfs`。
5. 体积检查：超过 `BANK*_APP_SIZE` 直接拒绝（OTA 的
   `app_ota_check_section_size` 会同样拒绝，早失败比烧完才发现好）。
6. `prepend_header /app_code` 更新 `payload_len`；`--sign` 时顺带重做 ECDSA 签名
   （改了 payload 必须重签，否则 boot 校验不过）。
7. 可选 `--mp` 加 MP 头（供 mpcli 识别下载地址）。
8. **写 `romfs_layout.h`**（`app/designer/src/` 或新版 Designer 的 `src/user/`，两处都存在时同步写）—— 固件挂载要用的 `ROMFS_OFFSET`。
9. 自检：重新解析产物头 + 重定位后的 romfs 指针。

### romfs 落点：偏移，不是绝对地址

默认紧跟 app 末尾（64 对齐）。固件用**运行时**表达式挂载：

```c
extern unsigned char __rom_region_start[];   /* linker: 本镜像加载基址 */
gui_vfs_mount_romfs("/", (void *)(__rom_region_start + ROMFS_OFFSET), 0);
```

原因：`__rom_region_start` 在 bank0 是 `0x7009E000`、bank1 是 `0x704F3000`，
而 romfs 与 app 间距固定。写成绝对常量会让**另一个 bank** 指错位置——症状是
目录项乱码 / 打开文件失败，且只在切 bank 后出现，很难查。

> 注：`gui_vfs_mount_romfs()` 的第三参数 `base_offset` 在实现里没被使用，
> 所以不能靠它做基址修正；指针必须在镜像里就是对的。

### 为什么不能沿用旧的固定偏移 `0x498400`

bank0 旧 romfs 在 `0x70536400`，距 app 基址 `0x498400`。但 app 末尾只到
`0x7018FE84`——中间 3.6MB 是空白。合并时这些空白会变成真实的填充字节，
比整个 app 分区（1.41MB）还大，永远过不了体积检查。

---

## 3. `fdb` —— 预制壁纸（FlashDB 格式）

素材默认放在 `fdb_resource_bin/`（随工程管理），产物也写到同一目录：

```bash
python3 tools/pack/pack.py fdb \
    --out tools/pack/fdb_resource_bin/fdb_prebuilt.bin \
    --file wp1=tools/pack/fdb_resource_bin/wp-1.bin \
    --file wp2=tools/pack/fdb_resource_bin/wp-2.bin
```

产物覆盖 `kvdb + bf_data` 两段，烧到 `layout.json` 里的 `kvdb_addr`：

```bash
mpcli -c /dev/ttyUSB0 -p -A 0x70936000 -F tools/pack/fdb_resource_bin/fdb_prebuilt.bin -M 5 -r -u -d -T RTL87X3G
```

### 为什么用 host 编译，而不是脚本拼二进制

KVDB 的扇区头是**逐位状态表**（`0xFF → 0x7F → 0x3F …`）、KV 头带 CRC、
还有 GC 与记录版本；BF 目录项是 KV 值，`offset` 相对 bf_data 起点且 `capacity`
按 block 对齐。手抄任何一处和库版本对不上，都是**静默脏数据**——开机看起来
"数据库坏了"，却查不出哪一步写错。

所以 `fdb` 子命令直接编译固件用的那套 FlashDB（`component/FlashDB`），
只把最底层的 FAL flash 操作换成对文件读写。产出的字节与设备自己
`fdb_bf_create/append/commit` 写出来的完全一致。

生成后会自动**读回校验**（`fdb_host_verify`）：重新 `kvdb_init → bf_init → bf_foreach`，
打印的内容就是开机 `bf_boot_enum_cb()` 会看到的东西；缺 key 或镜像被自动格式化都算失败。

### 资源 blob 的格式

一条 BF 记录 = 一个资源组：

| 段 | 长度 | 说明 |
|---|---|---|
| EBXF 头 | 40 B | magic/version/type/name/size/crc32；仅组的最外层有 |
| 资源索引头 | 40 B | 见下表 |
| padding | 0～7 B | 槽长度不是 8 的倍数时补 0，保证每个槽地址 8 字节对齐 |
| data GUI | — | `gui_rgb_data_head_t`(8) + size(4) + pad(4) + 图像本体 |
| preview GUI | — | 同上，可缺省；**SRC_TEARBADGE 固定 80 KB、4K 对齐、可由设备原地改写** |
| mask GUI | — | 仅 SRC_TEARBADGE |
| part GUI | — | 仅 SRC_TEARBADGE |

> 对 `SRC_TEARBADGE`，preview 前会补 0 到 4K 边界，并预留 `TEAR_PREVIEW_CAP`
>（80 KB，尾 0xFF）。索引里的 preview 长度是**容量**，实际图像大小由槽内
> GUI 头的 `size` 给出。设备保存时只擦写这 80 KB，记录本身和 `.raw` 不变，
> 因此删除 `.raw` 就能释放整条 badge。设备改写后 EBXF 里的 crc32 会过期，
> 固件不校验它。

索引头（全部小端，地址是 **XIP 绝对地址**）：

| 偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 4 | `id`：`0x81` SRC_IMG / `0x82` SRC_VIDEO / `0x85` SRC_TEARBADGE |
| 4 | 4 | data 的 GUI 头地址 |
| 8 | 4 | preview 的 GUI 头地址（0 = 无） |
| 12 | 4 | tear_mask（仅 tear，0 = 无） |
| 16 | 4 | tear_part（仅 tear，0 = 无） |
| 20 | 4 | data 长度（0 = 无该槽） |
| 24 | 4 | preview 长度 |
| 28 | 4 | tear_mask 长度 |
| 32 | 4 | tear_part 长度 |
| 36 | 4 | reserved（0） |

长度是槽内容的真实字节数，**不含槽间对齐 padding**。旧记录没有长度字段
（20..23 当时是 reserved=0），读取时按 0 处理并回退到“邻居地址/记录末尾推算”。

索引头是 40 B，这样 `EBXF(40) + 索引头(40) = 80`，第一个槽的 GUI 头天然
8 字节对齐。后续槽长度不是 8 的倍数时打包器补 0。

固件直接读这张表，**不再按 key 配对**（`mainface_list_init()` 里
`idx = raw + 40`，然后取 4 个地址分别赋给 `data` / `img_preview` /
`param0` / `param1`）。

> **VIDEO**：data 指向视频本体，不经 GUI 头；preview/mask/part 忽略。
> EBXF 的 size/crc 对 VIDEO 可留空。

### 组语法

```bash
# 一个组：data 必填，pre/mask/part 可选
--file "wp1=wp-1.bin,pre=pre-1.bin"
--file "tb1=wp-tear.bin,pre=pre-1.bin,mask=tear_mask.bin,part=tear_part.bin"
```

`--assets-dir` 会自动把 `wp-N.bin` 与 `pre-N.bin` 按序号配对成组。

> **key 命名注意**：以 `tear` 开头会被固件当作用户上传的缩略图
> （`TEAR_BF_KEY_PREFIX`）路由走，所以 tear 组用 `tb1` 之类的前缀。

### CRC 覆盖范围

| CRC | 覆盖 |
|---|---|
| EBXF 头里的 crc32 | 索引头 + 各槽 GUI（EBXF 之后全部） |
| FDB 目录项 data_crc | 同上，`--crc-skip 80`（EBXF 40 + 索引头 40） |

**工具会自动封装**（默认开）。传什么格式都行——PNG / JPEG / GIF 会被嗅探
文件头并填好 `file_type` 与 GUI `type`；无法识别的按 BIN 原样存。

为什么必须封：固件读壁纸走 `ViewMainFace_user.c:296`

```c
data = (uint8_t *)raw + 40;                    // 硬编码跳过 40 字节
gui_img_create_from_mem(..., data, ...);       // data 处必须是 GUI 图像头
```

裸存图片的话 `raw+40` 会落进图片中间，`type/w/h` 全是垃圾，UI 显示不出来。

相关选项：

| 选项 | 默认 | 说明 |
|---|---|---|
| `--no-wrap` | 关 | 不做封装，原样存（固件读不出，仅调试） |
| `--img-size WxH` | `466x466` | GUI 头里的宽高。解码器会用图像真实尺寸覆盖 |
| `--crc-skip N` | `80` | 目录项 CRC 的起始偏移（= EBXF 40 + 索引头 40） |

开机枚举（`bf_boot_enum_cb()`）只读目录项、不校验 `data_crc`，所以这个值
只影响读回校验。上传路径（`xfer_session.c`）写的是另一种记录，用
`ebadge_port_storage_wp_commit(..., EBXF_HDR_LEN, ...)`（跳过 40 字节）。

> 工具**不做转码**：PNG 仍是 PNG、JPEG 仍是 JPEG。固件链接了 stb_image
> 的 PNG/JPEG 解码器，两者都能显示。要减小编码体积请先用 Pillow 等转成
> 低质量 JPEG 再喂进来。

---

## 4. 接进编译脚本

```bash
# WSL / Linux
./tools/scripts/build.sh --bank 0 --pack                      # 用默认 romfs
./tools/scripts/build.sh --bank 0 --pack --romfs /path/x.bin
./tools/scripts/build.sh --bank both --pack                   # 两个 bank 都打
```

```powershell
# Windows
.\tools\scripts\build.ps1 -Bank 0 -Pack
.\tools\scripts\build.ps1 -Bank 0 -Pack -Romfs D:\path\x.bin
.\tools\scripts\build.ps1 -Bank both -Pack
```

`--pack` 会迭代到收敛：打包 → 若 `ROMFS_OFFSET` 变了就增量重编 → 再打包。
通常 1–2 轮（增量重编约 15 s），最多 6 轮。

体积检查失败时**不会**改写 `romfs_layout.h`，所以失败的打包不会污染固件配置。

---

## 5. 只烧 romfs（改壁纸后的快速通道）

`--pack` 会把 romfs 放到**4KB 对齐**的地址，使它独占完整擦除扇区、不与 app 共用。
于是改素材后可以只烧 1.58MB 的 romfs，而不必重烧 2.52MB 的合并镜像。

```bash
# WSL / Linux
./tools/scripts/build.sh --bank 0 --pack      # 生成 bin/bank0/app_romfs_bank0_reloc.bin
./tools/scripts/flash_romfs.sh                # 只烧 romfs（默认 bank0 / /dev/ttyUSB0）
./tools/scripts/flash_romfs.sh --bank 1 --port /dev/ttyUSB1
./tools/scripts/flash_romfs.sh --dry-run      # 只打印命令
```

```powershell
# Windows
.\tools\scripts\build.ps1 -Bank 0 -Pack
.\tools\scripts\flash_romfs.ps1 -Bank 0
.\tools\scripts\flash_romfs.ps1 -Bank 1 -Port COM7
.\tools\scripts\flash_romfs.ps1 -DryRun
```

**必须用 `_reloc.bin`**，不能用 `app/designer/build/app_romfs_*.bin`：
后者的目录项还是 mkromfs 时的旧绝对地址（`0x70536400`），烧上去 UI 全黑。
`_reloc.bin` 是已重定位副本，与合并镜像里的那一段逐字节相同。

`tools/scripts/flash_romfs.sh` 在烧之前会拒绝两种情况：

- `ROMFS_OFFSET` 不是 4KB 的倍数 → 擦除会波及 app 所在扇区
- reloc 文件首项目录指针 ≠ app 基址 + 当前偏移 → 文件是别的偏移打出来的

## 6. 已知限制

- **user data 不是 A/B**：`dfu_get_user_data_info()` 直接返回 `USER_DATA1_ADDR`，
  OTA 原地写。因此"app 与资源一致"成立，但**掉电不可回滚**——romfs 写一半掉电，
  新 app 就没有 UI 资源了。要原子性需要给 user data 做 A/B，32M 下要占两倍空间。
- **预制壁纸与用户壁纸同在 BF 命名空间**（key `wp<N>`）。"清除数据"
  （`fdb_bf_reset(FDB_BF_RESET_DIR_ONLY)`）会把预制壁纸一起删掉。
  若这不可接受，出厂固定的资源应放 romfs，BF 只放用户内容。
- `layout.json` 里带 `_todo` 的项是已知待修（当前是 FDB 区坐标）。
