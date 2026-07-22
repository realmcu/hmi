# POSIX 设备抽象层

为嵌入式 RTOS 提供一个 **POSIX 风格的外设操作接口**，让应用层与底层芯片/OS 解耦。

```
#define POSIX_API_VERSION  "1.1"
#define POSIX_API_FUNCS    "open/close/read/write/ioctl"
#define POSIX_API_COVER    "UART/GPIO/SPI/PWM/ADC/SDIO/LCD/TOUCH/GSENSOR"
#define POSIX_API_EXTEND   "用户自定义设备 (magic 0xF0~0xFF)"
```

## 设计目标

| 目标 | 方案 |
|------|------|
| 接口稳定 | 应用代码只调 `posix_open/read/write/ioctl/close` 5 个函数 |
| 芯片无关 | 所有硬件差异封装在 `port/` 目录中 |
| RTOS 无关 | 锁/ISR 检测通过弱符号覆盖 |
| 易于扩展 | 加新设备类型 = 1 个 h + 1 个 .c + 1 行注册 |
| 接近 Linux 风格 | 接口**形似** Linux，但语义并不等价（见下方「与 Linux 的差异」），不能靠 sed 直接迁移 |
| 零动态分配 | 核心用静态数组，驱动用静态池 |

## 架构

```
┌──────────────────────────────────────────────────┐
│              应用代码 (app/*.c)                    │
│  只能调用 posix_open/read/write/ioctl/close       │
├──────────────────────────────────────────────────┤
│            POSIX 核心 (core/posix_device.c)        │
│   设备表管理 → fd 分配 → ops 派发 → ISR 检测       │
├──────────────────────────────────────────────────┤
│        自动初始化 (core/posix_init.c)              │
│   链接段收集 → 按优先级调用各驱动注册函数          │
├──────────────────────────────────────────────────┤
│           驱动函数表 (posix_driver_ops_t)           │
│   { .open .close .read .write .ioctl }            │
├──────────────────────────────────────────────────┤
│          port/ 平台实现 (唯一需要修改的)             │
│   UART|GPIO|SPI|PWM|ADC|SDIO|LCD|TOUCH|GSENSOR   │
├──────────────────────────────────────────────────┤
│               芯片硬件寄存器 / RTOS API             │
└──────────────────────────────────────────────────┘
```

## 文件结构

```
posix-io-abstraction/
├── README.md
├── PORTING.md                            ← 移植指南（必读）
├── include/
│   ├── posix.h                           ← 统一入口
│   ├── posix_types.h                     ← 类型/错误码
│   ├── posix_device.h                    ← 核心接口 + 驱动 ops
│   ├── posix_ioctl.h                     ← ioctl 编码宏
│   ├── posix_init.h                      ← 自动注册宏机制
│   └── ioctls/
│       ├── posix_ioctl_uart.h            ← UART 配置结构体 + cmd
│       ├── posix_ioctl_spi.h
│       ├── posix_ioctl_gpio.h
│       ├── posix_ioctl_pwm.h
│       ├── posix_ioctl_adc.h
│       ├── posix_ioctl_sdio.h
│       ├── posix_ioctl_lcd.h
│       ├── posix_ioctl_touch.h
│       └── posix_ioctl_gsensor.h
├── core/
│   ├── posix_device.c                    ← 核心实现（不改）
│   └── posix_init.c                      ← auto_init 遍历实现
└── examples/
    ├── demo/                             ← 应用层用法示例
    │   ├── 01_uart_example.c             ← UART 收发 + 中断回调
    │   ├── 02_gpio_example.c             ← GPIO 引脚级 fd
    │   ├── 03_spi_example.c              ← SPI 全双工传输
    │   ├── 04_pwm_example.c              ← PWM 舵机控制
    │   ├── 05_adc_example.c              ← ADC 单次/连续采样
    │   ├── 06_sdio_example.c             ← SDIO 块读写
    │   ├── 07_lcd_example.c              ← LCD 刷屏
    │   ├── 08_touch_example.c            ← Touch 触摸点读取
    │   ├── 09_gsensor_example.c          ← G-sensor 三轴加速度
    └── port/                             ← 平台移植实现
        ├── posix_init_zephyr.ld          ← Zephyr 链接脚本参考
        ├── custom-rtos/                  ← 移植模板（新 RTOS 参照此目录）
        │   ├── posix_port.h
        │   ├── posix_port_init.c         ← 锁 + ISR 检测
        │   ├── posix_port_uart.c
        │   ├── posix_port_gpio.c
        │   ├── posix_port_spi.c
        │   ├── posix_port_pwm.c
        │   ├── posix_port_adc.c
        │   ├── posix_port_sdio.c
        │   ├── posix_port_lcd.c
        │   ├── posix_port_touch.c
        │   └── posix_port_gsensor.c
        └── zephyr-rtk/                   ← RTK8773G + Zephyr 实现
            ├── posix_port.h
            ├── posix_port_init.c
            ├── posix_port_uart.c
            ├── posix_port_gpio.c
            ├── posix_port_spi.c
            ├── posix_port_lcd.c
            ├── posix_port_touch.c
            └── posix_port_gsensor.c
```

## 极速开始

### 1. 加入你的项目

```makefile
# Makefile 示例
INCDIRS += posix-io-abstraction/include
SRCDIRS += posix-io-abstraction/core
SRCDIRS += posix-io-abstraction/port/custom-rtos    # 换成你的平台目录
```

### 2. 移植到你的 RTOS（约 360 行 C 代码）

```c
// port/your_rtos/posix_port_init.c

void posix_lock(void)   { /* your_rtos_mutex_lock(...) */ }
void posix_unlock(void) { /* your_rtos_mutex_unlock(...) */ }
int posix_port_in_isr(void) { /* return your_rtos_in_isr(); */ }
```

详细步骤见 [PORTING.md](PORTING.md)。

### 3. 初始化 + 写应用

```c
// main.c
#include "posix_port.h"
#include "ioctls/posix_ioctl_uart.h"
#include "ioctls/posix_ioctl_gpio.h"

void main(void)
{
    posix_port_init_all();

    posix_fd_t uart = posix_open("/dev/uart0");
    posix_write(uart, "Hello\r\n", 7);

    posix_fd_t led = posix_open("/dev/gpio0/p12");
    posix_gpio_config_t cfg = { .direction = 1 };
    posix_ioctl(led, POSIX_GPIO_IOCTL_SET_DIR, &cfg);
    posix_write(led, &(int){1}, sizeof(int));
}
```

## 接口全景

| 函数 | 作用 | 所有设备通用 |
|------|------|:---:|
| `posix_open(path)` | 打开设备 | ✓ |
| `posix_close(fd)` | 关闭设备 | ✓ |
| `posix_read(fd, buf, len)` | 读取数据 | UART/SPI/GPIO/ADC/SDIO |
| `posix_write(fd, buf, len)` | 写入数据 | UART/SPI/GPIO/SDIO |
| `posix_ioctl(fd, cmd, arg)` | 控制/配置 | 全部（cmd 不同） |
| `posix_read_isr(...)` | ISR 中读 | 同 read，但不加锁 |
| `posix_write_isr(...)` | ISR 中写 | 同 write，但不加锁 |
| `posix_ioctl_isr(...)` | ISR 中控制 | 同 ioctl，但不加锁；上下文用 posix_port_in_isr() 判断 |
| `posix_auto_init()` | 自动注册所有设备 | 启动时调一次 |
| `posix_port_init_all()` | 初始化锁 + auto_init | 启动入口 |

## 新增设备类型步骤

添加任意新设备类型只需 4 步：

```
1. 在 include/posix_device.h 中定义 magic（0x0A~0xEF 可用）
2. 在 include/ioctls/ 下写 ioctl 头文件
3. 实现 posix_driver_ops_t 的 5 个函数
4. 调用 posix_device_register() 注册 + POSIX_INIT_DEVICE_EXPORT()
核心框架（include/*, core/*）和 port 层不需要改一行。
```

📝 参考模板: `examples/port/custom-rtos/` 下任意驱动文件（如 `posix_port_uart.c`）

## 线程安全

| 级别 | 覆盖范围 | 说明 |
|------|----------|------|
| ✅ **框架层** | `posix_open/close/read/write/ioctl` | 锁内完成句柄校验+查表+取快照，**释放锁后**再调用驱动 ops（故 ops 可安全阻塞，不会串行化其它设备） |
| ⚠️ **驱动层** | 驱动内部对硬件寄存器的访问 | 由驱动实现自行保证 |
| 🔒 **ISR 版本** | `posix_*_isr()` | 跳过框架锁，仅用于中断上下文 |

**框架锁**通过弱符号提供默认空实现（`void posix_lock/unlock(void) {}`），
移植到多线程 RTOS 时只需在 `posix_port_init.c` 中覆盖为互斥锁实现：

```c
void posix_lock(void)   { rtos_mutex_lock(&g_mutex); }
void posix_unlock(void) { rtos_mutex_unlock(&g_mutex); }
```

> ⚠️ `posix_*_isr()` 在 ISR 中使用，不会（也不能）加锁。
> 如果 ISR 和任务上下文共用同一 fd，驱动自身需要用原子操作或关中断保护关键区。

## 设计决策

1. **为什么用 `posix_` 前缀？** 避免和 libc 符号冲突、自文档化。注意它**不是** Linux 原生 API 的别名：去掉前缀也不等价（见下方「与 Linux 的差异」）。

2. **为什么 gpio 用引脚级 fd？** `/dev/gpio0/p12` 风格，每个引脚独立 fd，和 Linux gpiolib 一致。适合引脚数多的场景。

3. **为什么 ioctl cmd 编码用 magic+nr？** 类似 Linux `_IOR/_IOW` 方案，设备类型在 cmd 值中编码，驱动内部 switch 分发，加新类型不冲突。

4. **为什么 ISR 安全版本是独立函数？** 因为 `posix_*_isr` 跳过框架锁（互斥量在 ISR 不可用）直接调 ops。**上下文判断统一用 `posix_port_in_isr()`**；框架不再往 cmd 里塞标志位——旧版 `posix_ioctl_isr` 会 OR 进 `POSIX_FLAG_ISR`，污染 cmd 命名空间且要求每个驱动手动屏蔽，已废弃。

5. **为什么核心用静态数组？** 嵌入式环境不一定有堆。`POSIX_DEVICE_TABLE_SIZE`（默认 32）和 `POSIX_FD_POOL_SIZE`（默认 16）可在编译前配置。

6. **为什么需要自动初始化（posix_auto_init）？** 驱动注册通过 GCC `__attribute__((section))` 将初始化函数指针放入特定 ELF 段，启动时遍历调用。这使得添加新驱动时，不需要修改任何核心代码或 port 初始化函数。链接脚本需保留 `.posix$init*` 段。

## 与 Linux 的差异（不能直接 sed 迁移）

本层是 **POSIX 风格**，不是 POSIX 兼容。与真 Linux 的关键差异：

| 方面 | 本层 | Linux 用户态 |
|------|------|--------------|
| `open` | `posix_open(path)`，1 个参数，返回不透明指针，失败返回 `POSIX_FD_NULL` | `open(path, flags[, mode])`，返回 int fd，失败 `-1`+`errno` |
| `read/write` 返回 | `posix_ssize_t`：`>=0` 字节数，`<0` 为 `POSIX_ERR_*` 负错误码 | `ssize_t`：`-1` 表错误，错误码在 `errno` |
| 错误模型 | 负返回值（内核风格），无全局 `errno` | `-1` + 全局 `errno` |
| `ioctl` cmd | 自定义 `magic<<8 \| nr`，非 `_IOC` 编码 | `_IOR/_IOW/...` 的 `_IOC` 编码 |

因此 `sed 's/posix_//g'` **不能**直接迁移：会缺 `open` 的 flags/mode、返回类型不匹配、错误处理逻辑全错。若要迁移到 Linux，需要一层适配 shim 而非纯文本替换。

## License

MIT
